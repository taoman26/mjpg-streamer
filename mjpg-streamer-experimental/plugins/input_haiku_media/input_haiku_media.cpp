/*
 * input_haiku_media.cpp — mjpg-streamer input plugin using Haiku Media Kit
 *
 * Thin bridge to BubiCam's WebcamKit (WebcamRoster/WebcamDevice), which
 * already implements a proven, crash-free capture pipeline (BMediaEventLooper
 * based consumer, buffer group setup, dormant-node instantiation, etc.).
 * This plugin only has to: enumerate a device, start capture into a small
 * BLooper, and JPEG-encode + publish each received frame.
 *
 * Build:  CMakeLists.txt in this directory (Haiku only) links libwebcam.so
 *         from the sibling BubiCam checkout.
 * Usage:  mjpg_streamer -i "input_haiku_media.so [-d node_name]"
 *                        -o "output_http.so"
 */

#ifdef __HAIKU__

// ── Haiku C++ headers ───────────────────────────────────────────────────────
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <DataIO.h>
#include <OS.h>
#include <TranslatorRoster.h>
#include <TranslatorFormats.h>

// BubiCam's WebcamKit
#include <WebcamKit.h>

// ── C headers ───────────────────────────────────────────────────────────────
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

// ── mjpg-streamer interface ─────────────────────────────────────────────────
extern "C" {
#include "mjpg_streamer.h"
#include "utils.h"
}

// ── Plugin bookkeeping ──────────────────────────────────────────────────────
#define PLUGIN_NAME "input_haiku_media"
#undef LOG
#define LOG(fmt, ...) \
    fprintf(stderr, "[" PLUGIN_NAME "] " fmt "\n", ##__VA_ARGS__)

static globals *pglobal = NULL;
static BApplication *gApp = NULL;

struct plugin_ctx {
    int            id;
    char           node_name[256]; /* optional: filter by device name */
    volatile bool  running;
    thread_id      setup_tid;
};

static plugin_ctx gCtx;

// ══════════════════════════════════════════════════════════════════════════════
// JPEG encode + publish into mjpg-streamer's shared buffer
// ══════════════════════════════════════════════════════════════════════════════
static void
EncodeAndPublish(int id, BBitmap *bitmap)
{
    if (!pglobal || bitmap == NULL)
        return;

    /* BBitmapStream takes ownership of 'bitmap' and deletes it on
       destruction - caller must not delete it separately. */
    BBitmapStream bitmapStream(bitmap);
    BMallocIO jpegOut;

    status_t err = BTranslatorRoster::Default()->Translate(
        &bitmapStream, NULL, NULL, &jpegOut, B_JPEG_FORMAT);

    if (err != B_OK) {
        LOG("JPEG translate failed: %s", strerror(err));
        return;
    }

    const void *jpegData = jpegOut.Buffer();
    size_t      jpegSize = jpegOut.BufferLength();

    if (!jpegData || jpegSize == 0)
        return;

    pthread_mutex_lock(&pglobal->in[id].db);

    if (pglobal->in[id].buf != NULL)
        free(pglobal->in[id].buf);

    pglobal->in[id].buf = (unsigned char *)malloc(jpegSize);
    if (pglobal->in[id].buf == NULL) {
        pglobal->in[id].size = 0;
        pthread_mutex_unlock(&pglobal->in[id].db);
        return;
    }

    memcpy(pglobal->in[id].buf, jpegData, jpegSize);
    pglobal->in[id].size = (int)jpegSize;

    struct timeval ts;
    gettimeofday(&ts, NULL);
    pglobal->in[id].timestamp = ts;

    pthread_cond_broadcast(&pglobal->in[id].db_update);
    pthread_mutex_unlock(&pglobal->in[id].db);
}

// ══════════════════════════════════════════════════════════════════════════════
// MjpgLooper: receives MSG_WEBCAM_FRAME from WebcamDevice::StartCapture()
// ══════════════════════════════════════════════════════════════════════════════
class MjpgLooper : public BLooper {
public:
    MjpgLooper(int id)
        : BLooper("mjpg_webcam_capture"), fId(id)
    {
    }

    virtual void MessageReceived(BMessage *message)
    {
        if (message->what != MSG_WEBCAM_FRAME) {
            BLooper::MessageReceived(message);
            return;
        }

        BBitmap *bitmap = NULL;
        if (message->FindPointer("bitmap", (void **)&bitmap) != B_OK)
            return;

        if (bitmap == NULL)
            return;

        if (bitmap->IsValid())
            EncodeAndPublish(fId, bitmap);
        else
            delete bitmap;
    }

private:
    int fId;
};

// ══════════════════════════════════════════════════════════════════════════════
// Setup thread: enumerate device, start capture, run until stopped
// ══════════════════════════════════════════════════════════════════════════════
static int32
setup_thread_func(void *arg)
{
    plugin_ctx *c = (plugin_ctx *)arg;

    if (be_app == NULL)
        gApp = new BApplication("application/x-vnd.mjpg-haiku-input");

    WebcamRoster *roster = new WebcamRoster();
    roster->EnumerateDevices();

    if (roster->CountDevices() == 0) {
        LOG("No webcams found");
        delete roster;
        return B_ERROR;
    }

    WebcamDevice *device = NULL;
    if (c->node_name[0] != '\0')
        device = roster->DeviceByName(c->node_name);
    if (device == NULL)
        device = roster->DeviceAt(0);

    LOG("Using device '%s'", device->Name());

    MjpgLooper *looper = new MjpgLooper(c->id);
    looper->Run();

    status_t err = device->StartCapture(looper);
    if (err != B_OK) {
        LOG("StartCapture failed: %s", strerror(err));
        looper->Lock();
        looper->Quit();
        delete roster;
        return B_ERROR;
    }

    LOG("Capture started");

    while (!pglobal->stop && c->running)
        snooze(200000);

    LOG("Stopping capture");
    device->StopCapture();
    looper->Lock();
    looper->Quit();
    delete roster;

    return B_OK;
}

// ══════════════════════════════════════════════════════════════════════════════
// mjpg-streamer plugin interface
// ══════════════════════════════════════════════════════════════════════════════
extern "C" int
input_init(input_parameter *param, int id)
{
    gCtx.id          = id;
    gCtx.node_name[0]= '\0';
    gCtx.running     = false;

    param->argv[0] = (char *)PLUGIN_NAME;

    reset_getopt();
    while (1) {
        int option_index = 0, c = 0;
        static struct option long_options[] = {
            {"h",    no_argument,       0, 0},
            {"help", no_argument,       0, 0},
            {"d",    required_argument, 0, 0},
            {0, 0, 0, 0}
        };

        c = getopt_long_only(param->argc, param->argv, "", long_options, &option_index);
        if (c == -1)
            break;

        if (c == '?') {
            LOG("Usage: mjpg_streamer -i \"input_haiku_media.so [-d node_name]\"");
            return 1;
        }

        switch (option_index) {
        case 0:
        case 1:
            LOG("Usage: mjpg_streamer -i \"input_haiku_media.so [-d node_name]\"");
            return 1;

        case 2:
            strlcpy(gCtx.node_name, optarg, sizeof(gCtx.node_name));
            break;

        default:
            break;
        }
    }

    pglobal = param->global;
    pglobal->in[id].name = strdup(PLUGIN_NAME);

    LOG("init OK  node_filter='%s'", gCtx.node_name);

    return 0;
}

extern "C" int
input_run(int id)
{
    pglobal->in[id].buf = NULL;
    gCtx.running = true;

    gCtx.setup_tid = spawn_thread(setup_thread_func, "haiku_media_setup",
                                   B_NORMAL_PRIORITY, &gCtx);
    if (gCtx.setup_tid < 0) {
        LOG("spawn_thread failed: %s", strerror(gCtx.setup_tid));
        return 1;
    }
    resume_thread(gCtx.setup_tid);

    LOG("run OK");
    return 0;
}

extern "C" int
input_stop(int id)
{
    gCtx.running = false;
    return 0;
}

#endif /* __HAIKU__ */
