mjpg-streamer
=============

Currently no issues are known, but since this software is quite young and not used widely it may cause problems. You must really know what you are doing, if you use this software. If you want to use the software you are obliged to check if the sourcecode does what you expect it to do and take the risk yourself to use it.


Usage
=====

When launching mjpg-streamer, you specify one or more input plugins and an output plugin. For example, to stream a V4L compatible webcam via an HTTP server (the most common use case), you
can do something like this:

	mjpg_streamer -i input_uvc.so -o output_http.so

Each plugin supports various options, you can view the plugin's options via its `--help` option:

	mjpg_streamer -i 'input_uvc.so --help'


More examples can be found in the start.sh bash script.

Plugin documentation
====================

Input plugins:

* input_file
* input_http
* input_haiku_media ([documentation](plugins/input_haiku_media/README.md))
* input_opencv ([documentation](plugins/input_opencv/README.md))
* input_ptp2
* input_raspicam ([documentation](plugins/input_raspicam/README.md))
* input_uvc ([documentation](plugins/input_uvc/README.md))

Output plugins:

* output_file
* output_http ([documentation](plugins/output_http/README.md))
* ~output_rtsp~ (not functional)
* ~output_udp~ (not functional)
* output_viewer ([documentation](plugins/output_viewer/README.md))


Building on Haiku
=================

mjpg-streamer can be built on [Haiku OS](https://www.haiku-os.org/) with the following notes:

**What works on Haiku:**
- Core streamer binary (`mjpg_streamer`)
- `input_http` plugin
- `output_file`, `output_http`, `output_rtsp`, `output_udp` plugins

**What is not available on Haiku (Linux-only):**
- `input_uvc` — requires Video4Linux2 (`linux/videodev2.h`)
- `input_file` — requires `sys/inotify.h`
- `input_raspicam` — Raspberry Pi specific
- `output_viewer` — requires SDL
- `output_zmqserver` — requires ZeroMQ

**Fixes applied for Haiku compatibility:**
- Updated `cmake_minimum_required` to VERSION 3.5 (required by modern CMake)
- Added `compat/linux/` shim headers providing `linux/types.h`, `linux/videodev2.h`, and `linux/version.h` for non-Linux platforms
- Fixed `utils.c`: replaced `<wait.h>` with `<sys/wait.h>`, removed `<linux/stat.h>`
- Removed `-ldl` linker flag on non-Linux (Haiku's dynamic linking is built into libroot)

**Build:**

    mkdir _build && cd _build && cmake -DCMAKE_BUILD_TYPE=Release .. && make

