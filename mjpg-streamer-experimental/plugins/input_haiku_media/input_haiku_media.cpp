/*
 * input_haiku_media.cpp — mjpg-streamer input plugin using Haiku Media Kit
 *
 * Connects to a live video producer node (e.g. haiku-uvc-webcam add-on)
 * via BMediaRoster, receives B_RGB32 frames, encodes to JPEG, and feeds
 * them into mjpg-streamer's shared frame buffer.
 *
 * Build:  CMakeLists.txt in this directory (Haiku only)
 * Usage:  mjpg_streamer -i "input_haiku_media.so [-r WxH] [-d node_name]"
 *                        -o "output_http.so"
 */

#ifdef __HAIKU__

// ── Haiku C++ headers ───────────────────────────────────────────────────────
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Buffer.h>
#include <BufferConsumer.h>
#include <DataIO.h>
#include <MediaDefs.h>
#include <MediaEventLooper.h>
#include <MediaFormats.h>
#include <MediaRoster.h>
#include <OS.h>
#include <TranslatorRoster.h>

// ── C headers ───────────────────────────────────────────────────────────────
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

// ── mjpg-streamer interface ─────────────────────────────────────────────────
// Include via root header to avoid double-include of plugins/input.h
extern "C" {
#include "mjpg_streamer.h"
}

// ── Plugin bookkeeping ──────────────────────────────────────────────────────
#define PLUGIN_NAME "input_haiku_media"
// Override the generic LOG macro with our prefixed version
#undef LOG
#define LOG(fmt, ...) \
    fprintf(stderr, "[" PLUGIN_NAME "] " fmt "\n", ##__VA_ARGS__)

static globals *pglobal = NULL;   /* set in input_init */

// ── Shared plugin state ─────────────────────────────────────────────────────
struct plugin_ctx {
    int            id;
    int            width;
    int            height;
    char           node_name[256]; /* optional: filter by name */
    volatile bool  running;
    thread_id      setup_tid;
    /* filled by setup thread once connected */
    media_node     producer_node;
    media_output   prod_out;
    media_input    cons_in;
    bool           connected;
};

static plugin_ctx    *ctx       = NULL;
static BApplication  *gApp      = NULL;  /* created if be_app is NULL */

// forward declaration
class MjpgConsumer;
static MjpgConsumer  *gConsumer = NULL;

// ══════════════════════════════════════════════════════════════════════════════
// MjpgConsumer: BBufferConsumer + BMediaEventLooper
// ══════════════════════════════════════════════════════════════════════════════
class MjpgConsumer : public BBufferConsumer, public BMediaEventLooper {
public:
    explicit MjpgConsumer(plugin_ctx *c);
    ~MjpgConsumer() override {}

    /* BMediaNode */
    BMediaAddOn *AddOn(int32 *) const override { return NULL; }

    /* BBufferConsumer */
    status_t AcceptFormat(const media_destination &dest,
                          media_format *fmt) override;
    status_t GetNextInput(int32 *cookie, media_input *out) override;
    void     BufferReceived(BBuffer *buffer) override;
    void     DisposeInputCookie(int32) override {}
    void     ProducerDataStatus(const media_destination &,
                                int32, bigtime_t) override {}
    status_t GetLatencyFor(const media_destination &,
                           bigtime_t *latency,
                           media_node_id *) override
    {
        *latency = 5000LL; /* 5 ms */
        return B_OK;
    }
    status_t Connected(const media_source &src,
                       const media_destination &dest,
                       const media_format &fmt,
                       media_input *out_input) override;
    void     Disconnected(const media_source &,
                          const media_destination &) override;
    status_t FormatChanged(const media_source &,
                           const media_destination &,
                           int32,
                           const media_format &) override { return B_OK; }

    /* BMediaEventLooper */
    void HandleEvent(const media_timed_event *,
                     bigtime_t, bool) override {}

    media_input &Input() { return fInput; }

    /* Public wrappers for protected Run()/Quit() */
    void StartLooper() { Run(); }
    void StopLooper()  { Quit(); }

private:
    void EncodeAndPublish(uint8 *rgb32, int w, int h);

    plugin_ctx *fCtx;
    media_input fInput;
    bool        fConnected;
};

// ── Constructor ─────────────────────────────────────────────────────────────
MjpgConsumer::MjpgConsumer(plugin_ctx *c)
    : BMediaNode("mjpg-haiku-consumer"),
      BBufferConsumer(B_MEDIA_RAW_VIDEO),
      BMediaEventLooper(),
      fCtx(c),
      fConnected(false)
{
    fInput.destination.port = ControlPort();
    fInput.destination.id   = 0;
    fInput.node             = Node();
    fInput.source           = media_source::null;
    strlcpy(fInput.name, "Video In", sizeof(fInput.name));
}

// ── AcceptFormat ─────────────────────────────────────────────────────────────
status_t
MjpgConsumer::AcceptFormat(const media_destination &, media_format *fmt)
{
    if (fmt->type != B_MEDIA_RAW_VIDEO &&
        fmt->type != B_MEDIA_UNKNOWN_TYPE)
        return B_MEDIA_BAD_FORMAT;

    fmt->type = B_MEDIA_RAW_VIDEO;
    fmt->u.raw_video.display.format = B_RGB32;
    return B_OK;
}

// ── GetNextInput ─────────────────────────────────────────────────────────────
status_t
MjpgConsumer::GetNextInput(int32 *cookie, media_input *out)
{
    if (*cookie != 0)
        return B_BAD_INDEX;
    *out = fInput;
    (*cookie)++;
    return B_OK;
}

// ── Connected ────────────────────────────────────────────────────────────────
status_t
MjpgConsumer::Connected(const media_source &src,
                        const media_destination &,
                        const media_format &fmt,
                        media_input *out_input)
{
    fInput.source = src;
    fInput.format = fmt;
    *out_input    = fInput;
    fConnected    = true;

    /* update dimensions from negotiated format */
    uint32 w = fmt.u.raw_video.display.line_width;
    uint32 h = fmt.u.raw_video.display.line_count;
    if (w > 0) fCtx->width  = (int)w;
    if (h > 0) fCtx->height = (int)h;
    LOG("Connected: %dx%d B_RGB32", fCtx->width, fCtx->height);
    return B_OK;
}

// ── Disconnected ─────────────────────────────────────────────────────────────
void
MjpgConsumer::Disconnected(const media_source &, const media_destination &)
{
    fConnected    = false;
    fInput.source = media_source::null;
    LOG("Disconnected");
}

// ── EncodeAndPublish: B_RGB32 → JPEG → pglobal ──────────────────────────────
void
MjpgConsumer::EncodeAndPublish(uint8 *rgb32, int w, int h)
{
    /* Wrap raw pixels in a BBitmap (B_RGB32 = BGRA byte order on Haiku) */
    BBitmap *bm = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGB32);
    if (!bm || bm->InitCheck() != B_OK) {
        delete bm;
        return;
    }
    size_t sz = (size_t)(w * h * 4);
    memcpy(bm->Bits(), rgb32, sz);

    /* Encode to JPEG via the Translation Kit (JPEG Translator must be installed) */
    BBitmapStream bs(bm);   /* bs takes ownership of bm */
    BMallocIO     jpegBuf;

    BTranslatorRoster *roster = BTranslatorRoster::Default();
    status_t err = roster->Translate(&bs, NULL, NULL, &jpegBuf, B_JPEG_FORMAT);

    /* detach so bs destructor doesn't double-free */
    BBitmap *detached = NULL;
    bs.DetachBitmap(&detached);
    delete detached;

    if (err != B_OK) {
        LOG("JPEG translation error: %s", strerror(err));
        return;
    }

    size_t  jpegSz   = jpegBuf.BufferLength();
    uint8  *jpegData = (uint8 *)malloc(jpegSz);
    if (!jpegData) return;
    memcpy(jpegData, jpegBuf.Buffer(), jpegSz);

    /* publish to mjpg-streamer */
    int id = fCtx->id;
    pthread_mutex_lock(&pglobal->in[id].db);
    free(pglobal->in[id].buf);
    pglobal->in[id].buf  = jpegData;
    pglobal->in[id].size = (int)jpegSz;
    gettimeofday(&pglobal->in[id].timestamp, NULL);
    pthread_cond_broadcast(&pglobal->in[id].db_update);
    pthread_mutex_unlock(&pglobal->in[id].db);
}

// ── BufferReceived ───────────────────────────────────────────────────────────
void
MjpgConsumer::BufferReceived(BBuffer *buffer)
{
    if (!fCtx->running || !fConnected) {
        buffer->Recycle();
        return;
    }

    uint8 *data = (uint8 *)buffer->Data();
    int    w    = fCtx->width;
    int    h    = fCtx->height;

    if (data && w > 0 && h > 0)
        EncodeAndPublish(data, w, h);

    buffer->Recycle();
}

// ══════════════════════════════════════════════════════════════════════════════
// Setup thread: find producer node, create consumer, connect, start
// ══════════════════════════════════════════════════════════════════════════════
static int32
setup_thread_func(void *arg)
{
    plugin_ctx *c = (plugin_ctx *)arg;

    /* Media Kit requires be_app; create a minimal one if missing */
    if (be_app == NULL)
        gApp = new BApplication("application/x-vnd.mjpg-haiku-input");

    BMediaRoster *roster = BMediaRoster::Roster();
    if (!roster) {
        LOG("Failed to get BMediaRoster");
        return B_ERROR;
    }

    /* find live video producer nodes */
    live_node_info nodes[16];
    int32          count = 16;
    media_format   filter;
    memset(&filter, 0, sizeof(filter));
    filter.type = B_MEDIA_RAW_VIDEO;

    status_t err = roster->GetLiveNodes(nodes, &count,
                                        &filter, NULL, NULL,
                                        B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
    if (err != B_OK || count == 0) {
        LOG("No live video producer found (err=%s, count=%d)",
            strerror(err), (int)count);
        return B_ERROR;
    }

    /* pick node: first match (or by name if specified) */
    int chosen = 0;
    if (c->node_name[0] != '\0') {
        for (int i = 0; i < count; i++) {
            if (strstr(nodes[i].name, c->node_name)) {
                chosen = i;
                break;
            }
        }
    }
    c->producer_node = nodes[chosen].node;
    LOG("Using producer: '%s' (id=%d)", nodes[chosen].name,
        (int)c->producer_node.node);

    /* create and register our consumer */
    gConsumer = new MjpgConsumer(c);
    err = roster->RegisterNode(gConsumer);
    if (err != B_OK) {
        LOG("RegisterNode failed: %s", strerror(err));
        return B_ERROR;
    }

    /* get free outputs from producer */
    media_output outputs[8];
    int32        nout = 8;
    err = roster->GetFreeOutputsFor(c->producer_node,
                                    outputs, nout, &nout,
                                    B_MEDIA_RAW_VIDEO);
    if (err != B_OK || nout == 0) {
        LOG("No free outputs on producer: %s", strerror(err));
        roster->UnregisterNode(gConsumer);
        return B_ERROR;
    }
    c->prod_out = outputs[0];

    /* request B_RGB32 at the configured resolution */
    media_format connFmt;
    memset(&connFmt, 0, sizeof(connFmt));
    connFmt.type                         = B_MEDIA_RAW_VIDEO;
    connFmt.u.raw_video.display.format   = B_RGB32;
    connFmt.u.raw_video.display.line_width  = (uint32)c->width;
    connFmt.u.raw_video.display.line_count  = (uint32)c->height;

    err = roster->Connect(c->prod_out.source,
                          gConsumer->Input().destination,
                          &connFmt,
                          &c->prod_out, &c->cons_in);
    if (err != B_OK) {
        LOG("Connect failed: %s", strerror(err));
        roster->UnregisterNode(gConsumer);
        return B_ERROR;
    }
    c->connected = true;
    LOG("Connection established: %dx%d", c->width, c->height);

    /* start consumer event loop BEFORE the producer sends buffers */
    gConsumer->StartLooper();

    bigtime_t latency = 0;
    roster->GetLatencyFor(c->producer_node, &latency);
    bigtime_t startTime = system_time() + latency + 50000LL;

    roster->StartNode(gConsumer->Node(), startTime);
    roster->StartNode(c->producer_node, startTime);
    LOG("Nodes started");

    /* keep this thread alive until input_stop() sets running = false */
    while (c->running)
        snooze(100000LL); /* 100 ms */

    /* ── teardown ── */
    roster->StopNode(c->producer_node, system_time(), true /* sync */);
    roster->StopNode(gConsumer->Node(), system_time(), true);
    roster->Disconnect(c->prod_out, c->cons_in);
    c->connected = false;

    roster->UnregisterNode(gConsumer);
    gConsumer->StopLooper();
    gConsumer = NULL;

    LOG("Setup thread exiting");
    return B_OK;
}

// ══════════════════════════════════════════════════════════════════════════════
// mjpg-streamer C plugin interface
// ══════════════════════════════════════════════════════════════════════════════
extern "C" {

int input_init(input_parameter *param, int id)
{
    pglobal = param->global;

    ctx = new plugin_ctx();
    memset(ctx, 0, sizeof(*ctx));
    ctx->id      = id;
    ctx->width   = 320;
    ctx->height  = 240;
    ctx->running = false;

    /* parse plugin arguments */
    for (int i = 1; i < param->argc; i++) {
        if (strcmp(param->argv[i], "-r") == 0 && i + 1 < param->argc) {
            int w = 0, h = 0;
            if (sscanf(param->argv[++i], "%dx%d", &w, &h) == 2 &&
                w > 0 && h > 0) {
                ctx->width  = w;
                ctx->height = h;
            }
        } else if (strcmp(param->argv[i], "-d") == 0 && i + 1 < param->argc) {
            strlcpy(ctx->node_name, param->argv[++i], sizeof(ctx->node_name));
        }
    }

    /* allocate placeholder frame buffer */
    pglobal->in[id].buf = (unsigned char *)malloc(
        (size_t)(ctx->width * ctx->height * 4));
    if (!pglobal->in[id].buf) {
        LOG("malloc failed");
        delete ctx; ctx = NULL;
        return 1;
    }
    pglobal->in[id].size = 0;

    LOG("init OK  resolution=%dx%d  node_filter='%s'",
        ctx->width, ctx->height, ctx->node_name);
    return 0;
}

int input_run(int id)
{
    ctx->running = true;
    ctx->setup_tid = spawn_thread(setup_thread_func,
                                  "haiku-media-setup",
                                  B_NORMAL_PRIORITY,
                                  ctx);
    if (ctx->setup_tid < B_OK) {
        LOG("spawn_thread failed");
        ctx->running = false;
        return 1;
    }
    resume_thread(ctx->setup_tid);
    LOG("run OK");
    return 0;
}

int input_stop(int id)
{
    LOG("stopping...");
    ctx->running = false;
    int32 ret = 0;
    wait_for_thread(ctx->setup_tid, &ret);

    free(pglobal->in[id].buf);
    pglobal->in[id].buf  = NULL;
    pglobal->in[id].size = 0;

    delete ctx;
    ctx = NULL;

    if (gApp) {
        delete gApp;
        gApp = NULL;
    }
    LOG("stopped");
    return 0;
}

int input_cmd(int id, unsigned int cmd, unsigned int value)
{
    return 0;
}

} /* extern "C" */

#endif /* __HAIKU__ */
