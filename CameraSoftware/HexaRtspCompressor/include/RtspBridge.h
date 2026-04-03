#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <string>
#include <atomic>

struct BridgeConfig {
    // Input RTSP stream
    std::string input_uri;
    bool input_use_tcp = false;

    // Output RTSP server
    std::string output_address = "0.0.0.0";
    int output_port = 8554;
    std::string output_path = "/stream";

    // Re-encoding settings (empty = passthrough / copy)
    std::string output_codec;       // e.g. "h264", "h265", "copy"
    int output_bitrate = 0;         // kbps, 0 = keep original
    int output_width = 0;           // 0 = keep original
    int output_height = 0;          // 0 = keep original
    double output_framerate = 0.0;  // 0 = keep original

    // Performance tuning
    int latency = 200;              // rtspsrc latency in ms
    bool drop_on_latency = true;    // drop frames when latency budget exceeded
    bool enable_queues = true;      // insert queue elements for parallelism
    int queue_max_buffers = 3;      // max buffers per queue (0 = unlimited)
    int queue_max_time_ms = 0;      // max time per queue in ms (0 = default)
    std::string queue_leaky;        // "no", "upstream", "downstream"
    std::string speed_preset = "ultrafast"; // encoder speed preset
    int encoder_threads = 0;        // encoder threads (0 = auto)
    bool sliced_threads = false;    // x264enc sliced-threads for parallelism
    int key_int_max = 0;            // max keyframe interval (0 = encoder default)
    std::string passthrough_codec = "h264"; // codec hint for passthrough depay/pay
};

/// Parse a JSON config file into a BridgeConfig struct.
/// Returns true on success, false on parse error.
bool parseBridgeConfig(const std::string& json_path, BridgeConfig& config);

class RtspBridge {
public:
    RtspBridge();
    ~RtspBridge();

    RtspBridge(const RtspBridge&) = delete;
    RtspBridge& operator=(const RtspBridge&) = delete;

    /// Start the RTSP bridge with the given configuration.
    /// @return true if the server started successfully.
    bool start(const BridgeConfig& config);

    /// Stop the bridge and release resources.
    void stop();

    /// Returns true while the bridge is running.
    bool isRunning() const;

private:
    std::atomic<bool> running_{false};
    GstRTSPServer* server_ = nullptr;
    GMainLoop* loop_ = nullptr;
    BridgeConfig config_;

    /// Build a queue element string with configured properties.
    std::string buildQueueElement() const;

    /// Build the GStreamer launch string for the media factory.
    std::string buildLaunchString() const;
};
