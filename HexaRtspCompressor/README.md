# HexaRtspCompressor

A C++ application that connects to an RTSP stream via GStreamer, collects video metadata (codec, resolution, framerate, format, transport), and monitors real-time bandwidth consumption.

## Dependencies

- **CMake** >= 3.16
- **GStreamer 1.0** development libraries:
  - `gstreamer-1.0`
  - `gstreamer-plugins-base` (video, app, pbutils, sdp, rtsp)
  - `gstreamer-plugins-good` (contains `rtspsrc`)
  - `gstreamer-plugins-bad` / `gstreamer-plugins-ugly` (optional, for extra codecs)
  - `gstreamer-rtsp-server` (required for `--bridge` mode)

### Install on Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y cmake build-essential pkg-config \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav
```

### Install on Fedora

```bash
sudo dnf install -y cmake gcc-c++ pkgconfig \
    gstreamer1-devel gstreamer1-plugins-base-devel \
    gstreamer1-rtsp-server-devel \
    gstreamer1-plugins-good gstreamer1-plugins-bad-free \
    gstreamer1-plugins-ugly gstreamer1-libav
```

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

The application has two modes: **Analyzer** (default) and **Bridge**.

### Analyzer Mode

```
./HexaRtspCompressor <rtsp_uri> [--tcp] [--view] [--duration <seconds>]
```

| Option              | Description                                      |
|---------------------|--------------------------------------------------|
| `<rtsp_uri>`        | Full RTSP URI (required)                         |
| `--tcp`             | Force RTP-over-TCP interleaved transport         |
| `--view`            | Open a window displaying the live video stream   |
| `--duration <sec>`  | Run for N seconds then stop (default: until Ctrl-C) |

### Bridge Mode

```
./HexaRtspCompressor --bridge <config.json>
```

Starts an RTSP server that pulls from an input RTSP stream and re-serves it. The JSON config file defines both the input and output stream parameters.

### Examples

```bash
# Analyzer: connect via UDP, run until Ctrl-C
./HexaRtspCompressor rtsp://admin:admin@192.168.1.10:554/stream1

# Analyzer: force TCP transport, run for 30 seconds
./HexaRtspCompressor rtsp://admin:pass@10.0.0.5:554/cam1 --tcp --duration 30

# Analyzer: open a video window while analysing
./HexaRtspCompressor rtsp://admin:pass@10.0.0.5:554/cam1 --view

# Bridge: passthrough (copy) mode
./HexaRtspCompressor --bridge config/bridge_example.json

# Bridge: transcode to 720p H264 at 1500 kbps
./HexaRtspCompressor --bridge config/bridge_transcode.json
```

### Bridge Config JSON Format

```json
{
    "input_uri": "rtsp://admin:admin@192.168.1.10:554/stream1",
    "input_use_tcp": true,

    "output_address": "0.0.0.0",
    "output_port": 8554,
    "output_path": "/stream",

    "output_codec": "copy",
    "output_bitrate": 0,
    "output_width": 0,
    "output_height": 0,
    "output_framerate": 0,

    "latency": 200,
    "drop_on_latency": true,
    "enable_queues": true,
    "queue_max_buffers": 3,
    "queue_max_time_ms": 0,
    "queue_leaky": "downstream",
    "speed_preset": "ultrafast",
    "encoder_threads": 0,
    "sliced_threads": false,
    "key_int_max": 0,
    "passthrough_codec": "h264"
}
```

**Stream settings:**

| Field              | Description                                                        |
|--------------------|--------------------------------------------------------------------|
| `input_uri`        | Source RTSP URI (required)                                         |
| `input_use_tcp`    | Force TCP for the input stream (`true`/`false`)                    |
| `output_address`   | Bind address for the output RTSP server (default `0.0.0.0`)       |
| `output_port`      | Port for the output RTSP server (default `8554`)                   |
| `output_path`      | Mount path for the output stream (default `/stream`)               |
| `output_codec`     | `"copy"` for passthrough, `"h264"` or `"h265"` for transcoding     |
| `output_bitrate`   | Target bitrate in kbps (0 = keep original, only for transcoding)   |
| `output_width`     | Output width (0 = keep original, only for transcoding)             |
| `output_height`    | Output height (0 = keep original, only for transcoding)            |
| `output_framerate` | Output framerate (0 = keep original, only for transcoding)         |

**Performance tuning:**

| Field               | Description                                                                  |
|----------------------|------------------------------------------------------------------------------|
| `latency`            | rtspsrc jitter-buffer latency in ms (default `200`)                          |
| `drop_on_latency`    | Drop frames when latency budget is exceeded (default `true`)                 |
| `enable_queues`      | Insert queue elements to parallelise pipeline stages (default `true`)        |
| `queue_max_buffers`  | Max buffers per queue, 0 = unlimited (default `3`)                           |
| `queue_max_time_ms`  | Max time per queue in ms, 0 = GStreamer default (default `0`)                |
| `queue_leaky`        | Queue leak policy: `"no"`, `"upstream"`, `"downstream"` (default `""`)       |
| `speed_preset`       | Encoder speed preset, e.g. `"ultrafast"`, `"superfast"`, `"fast"` (default `"ultrafast"`) |
| `encoder_threads`    | Encoder thread count, 0 = auto (default `0`, x264 only)                     |
| `sliced_threads`     | Enable x264 sliced-threads for lower latency (default `false`)              |
| `key_int_max`        | Max keyframe interval in frames, 0 = encoder default (default `0`)          |
| `passthrough_codec`  | Codec hint for passthrough depay/pay: `"h264"` or `"h265"` (default `"h264"`) |

### Sample Output

```
Connecting to: rtsp://admin:admin@192.168.1.10:554/stream1
Transport    : TCP
Video Window : enabled
Duration     : 30 seconds
----------------------------------------
[RtspAnalyzer] Pipeline state: null -> ready
[RtspAnalyzer] Pipeline state: ready -> paused
[RtspAnalyzer] Linked video pad (application/x-rtp).
[RtspAnalyzer] Pipeline state: paused -> playing

===== Video Stream Info =====
  Codec      : video/x-raw
  Resolution : 1920x1080
  Framerate  : 25 fps
  Format     : I420
  Transport  : RTP/TCP
=============================

[BW] current: 4521.3 kbps  avg: 4480.7 kbps  peak: 5102.0 kbps  total: 16384 KB  frames: 750  elapsed: 30.0 s

========== Final Report ==========
  Stream URI   : rtsp://admin:admin@192.168.1.10:554/stream1
  Codec        : video/x-raw
  Resolution   : 1920x1080
  Framerate    : 25.00 fps
  Pixel Format : I420
  Transport    : RTP/TCP
  --------------------------------
  Avg Bandwidth: 4480.7 kbps (4.5 Mbps)
  Peak Bandwidth: 5102.0 kbps (5.1 Mbps)
  Total Data   : 16384 KB (16 MB)
  Total Frames : 750
  Duration     : 30.0 s
==================================
```

## Architecture

### Analyzer Pipeline

```
# Without --view:
rtspsrc  -->  identity (probe)  -->  decodebin  -->  fakesink

# With --view:
rtspsrc  -->  identity (probe)  -->  decodebin  -->  videoconvert  -->  autovideosink

   identity has a buffer probe that measures bytes/sec for bandwidth stats.
   rtspsrc and decodebin use pad-added signals for dynamic linking.
```

- **rtspsrc**: Handles RTSP negotiation, SDP parsing, RTP/RTCP.
- **identity**: Pass-through element with a buffer probe to count bytes for bandwidth measurement.
- **decodebin**: Auto-negotiates RTP depayloading and video decoding.
- **fakesink**: Discards decoded frames (no rendering needed for analysis).
- **videoconvert + autovideosink**: (with `--view`) Converts and renders decoded video in a window.

### Bridge Pipeline

```
# Passthrough (codec = "copy"):
rtspsrc  -->  rtph264depay  -->  rtph264pay  -->  [RTSP Server]

# Transcode (codec = "h264" / "h265"):
rtspsrc  -->  decodebin  -->  videoconvert  -->  [videoscale]  -->  [videorate]
         -->  x264enc/x265enc  -->  rtph264pay/rtph265pay  -->  [RTSP Server]
```

- **GstRTSPServer**: Hosts the output stream; clients connect via standard RTSP.
- **Passthrough**: Depayloads and re-payloads RTP without decoding — minimal CPU.
- **Transcode**: Decodes, optionally scales/rate-limits, re-encodes, and serves.
