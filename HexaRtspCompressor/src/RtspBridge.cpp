#include "RtspBridge.h"
#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Config Parser
// ---------------------------------------------------------------------------

bool parseBridgeConfig(const std::string& json_path, BridgeConfig& config) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        std::cerr << "[BridgeConfig] Cannot open config file: " << json_path << "\n";
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[BridgeConfig] JSON parse error: " << e.what() << "\n";
        return false;
    }

    // Input
    if (j.contains("input_uri"))     j.at("input_uri").get_to(config.input_uri);
    if (j.contains("input_use_tcp")) j.at("input_use_tcp").get_to(config.input_use_tcp);

    // Output server
    if (j.contains("output_address"))   j.at("output_address").get_to(config.output_address);
    if (j.contains("output_port"))      j.at("output_port").get_to(config.output_port);
    if (j.contains("output_path"))      j.at("output_path").get_to(config.output_path);

    // Re-encoding
    if (j.contains("output_codec"))     j.at("output_codec").get_to(config.output_codec);
    if (j.contains("output_bitrate"))   j.at("output_bitrate").get_to(config.output_bitrate);
    if (j.contains("output_width"))     j.at("output_width").get_to(config.output_width);
    if (j.contains("output_height"))    j.at("output_height").get_to(config.output_height);
    if (j.contains("output_framerate")) j.at("output_framerate").get_to(config.output_framerate);

    // Performance tuning
    if (j.contains("latency"))            j.at("latency").get_to(config.latency);
    if (j.contains("drop_on_latency"))    j.at("drop_on_latency").get_to(config.drop_on_latency);
    if (j.contains("enable_queues"))      j.at("enable_queues").get_to(config.enable_queues);
    if (j.contains("queue_max_buffers"))  j.at("queue_max_buffers").get_to(config.queue_max_buffers);
    if (j.contains("queue_max_time_ms"))  j.at("queue_max_time_ms").get_to(config.queue_max_time_ms);
    if (j.contains("queue_leaky"))        j.at("queue_leaky").get_to(config.queue_leaky);
    if (j.contains("speed_preset"))       j.at("speed_preset").get_to(config.speed_preset);
    if (j.contains("encoder_threads"))    j.at("encoder_threads").get_to(config.encoder_threads);
    if (j.contains("sliced_threads"))     j.at("sliced_threads").get_to(config.sliced_threads);
    if (j.contains("key_int_max"))        j.at("key_int_max").get_to(config.key_int_max);
    if (j.contains("passthrough_codec"))  j.at("passthrough_codec").get_to(config.passthrough_codec);

    if (config.input_uri.empty()) {
        std::cerr << "[BridgeConfig] 'input_uri' is required in config.\n";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

RtspBridge::RtspBridge() {
    gst_init(nullptr, nullptr);
}

RtspBridge::~RtspBridge() {
    stop();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool RtspBridge::start(const BridgeConfig& config) {
    if (running_) {
        std::cerr << "[RtspBridge] Already running. Call stop() first.\n";
        return false;
    }

    config_ = config;

    server_ = gst_rtsp_server_new();
    if (!server_) {
        std::cerr << "[RtspBridge] Failed to create RTSP server.\n";
        return false;
    }

    gst_rtsp_server_set_address(server_, config_.output_address.c_str());

    gchar port_str[16];
    g_snprintf(port_str, sizeof(port_str), "%d", config_.output_port);
    gst_rtsp_server_set_service(server_, port_str);

    // Build the pipeline launch string.
    std::string launch = buildLaunchString();
    std::cout << "[RtspBridge] Launch string:\n  " << launch << "\n";

    // Create a media factory with the launch description.
    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    // Mount the factory at the configured path.
    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    gst_rtsp_mount_points_add_factory(mounts, config_.output_path.c_str(), factory);
    g_object_unref(mounts);

    // Attach the server to the default main context.
    if (gst_rtsp_server_attach(server_, nullptr) == 0) {
        std::cerr << "[RtspBridge] Failed to attach RTSP server.\n";
        g_object_unref(server_);
        server_ = nullptr;
        return false;
    }

    running_ = true;

    std::cout << "[RtspBridge] RTSP server started.\n"
              << "  Input  : " << config_.input_uri << "\n"
              << "  Output : rtsp://" << config_.output_address
              << ":" << config_.output_port << config_.output_path << "\n";

    return true;
}

void RtspBridge::stop() {
    if (!running_) return;

    running_ = false;

    if (server_) {
        g_object_unref(server_);
        server_ = nullptr;
    }

    std::cout << "[RtspBridge] Bridge stopped.\n";
}

bool RtspBridge::isRunning() const {
    return running_;
}

// ---------------------------------------------------------------------------
// Pipeline Launch String Builder
// ---------------------------------------------------------------------------

std::string RtspBridge::buildQueueElement() const {
    std::ostringstream q;
    q << "queue";
    if (config_.queue_max_buffers > 0) {
        q << " max-size-buffers=" << config_.queue_max_buffers;
    }
    if (config_.queue_max_time_ms > 0) {
        q << " max-size-time=" << (static_cast<uint64_t>(config_.queue_max_time_ms) * 1000000ULL);
    }
    if (!config_.queue_leaky.empty() && config_.queue_leaky != "no") {
        q << " leaky=" << config_.queue_leaky;
    }
    return q.str();
}

std::string RtspBridge::buildLaunchString() const {
    std::ostringstream ss;

    // Source: pull from the input RTSP stream.
    ss << "( rtspsrc location=" << config_.input_uri
       << " latency=" << config_.latency;

    if (config_.drop_on_latency) {
        ss << " drop-on-latency=true";
    }

    if (config_.input_use_tcp) {
        ss << " protocols=tcp";
    }

    ss << " ! ";

    // Determine codec for output.
    std::string codec = config_.output_codec;
    std::transform(codec.begin(), codec.end(), codec.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    bool passthrough = codec.empty() || codec == "copy";

    // Resolve passthrough codec hint.
    std::string pt_codec = config_.passthrough_codec;
    std::transform(pt_codec.begin(), pt_codec.end(), pt_codec.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (passthrough) {
        // Passthrough: depay and re-pay without decoding.
        if (config_.enable_queues) {
            ss << buildQueueElement() << " ! ";
        }

        if (pt_codec == "h265" || pt_codec == "hevc") {
            ss << "rtph265depay ! rtph265pay name=pay0 pt=96";
        } else {
            ss << "rtph264depay ! rtph264pay name=pay0 pt=96";
        }
    } else {
        // Transcode path: depay -> queue -> decode -> queue -> convert/scale/rate -> queue -> encode -> queue -> pay

        ss << "decodebin";

        // Queue after decode (decouple network from processing).
        if (config_.enable_queues) {
            ss << " ! " << buildQueueElement();
        }

        ss << " ! videoconvert";

        // Optional resolution scaling.
        if (config_.output_width > 0 && config_.output_height > 0) {
            ss << " ! videoscale ! video/x-raw,width="
               << config_.output_width << ",height=" << config_.output_height;
        }

        // Optional framerate adjustment.
        if (config_.output_framerate > 0.0) {
            int fps_n = static_cast<int>(config_.output_framerate);
            ss << " ! videorate ! video/x-raw,framerate=" << fps_n << "/1";
        }

        // Queue before encoder (isolate the slowest element).
        if (config_.enable_queues) {
            ss << " ! " << buildQueueElement();
        }

        // Encoder selection.
        if (codec == "h264") {
            ss << " ! x264enc tune=zerolatency"
               << " speed-preset=" << config_.speed_preset;
            if (config_.output_bitrate > 0) {
                ss << " bitrate=" << config_.output_bitrate;
            }
            if (config_.encoder_threads > 0) {
                ss << " threads=" << config_.encoder_threads;
            }
            if (config_.sliced_threads) {
                ss << " sliced-threads=true";
            }
            if (config_.key_int_max > 0) {
                ss << " key-int-max=" << config_.key_int_max;
            }
        } else if (codec == "h265" || codec == "hevc") {
            ss << " ! x265enc tune=zerolatency"
               << " speed-preset=" << config_.speed_preset;
            if (config_.output_bitrate > 0) {
                ss << " bitrate=" << config_.output_bitrate;
            }
            if (config_.key_int_max > 0) {
                ss << " key-int-max=" << config_.key_int_max;
            }
        } else {
            std::cerr << "[RtspBridge] Unknown codec '" << config_.output_codec
                      << "', falling back to H264.\n";
            ss << " ! x264enc tune=zerolatency"
               << " speed-preset=" << config_.speed_preset;
            if (config_.output_bitrate > 0) {
                ss << " bitrate=" << config_.output_bitrate;
            }
        }

        // Queue after encoder (decouple encoder from payloader).
        if (config_.enable_queues) {
            ss << " ! " << buildQueueElement();
        }

        // RTP payloader.
        if (codec == "h265" || codec == "hevc") {
            ss << " ! rtph265pay name=pay0 pt=96";
        } else {
            ss << " ! rtph264pay name=pay0 pt=96";
        }
    }

    ss << " )";
    return ss.str();
}
