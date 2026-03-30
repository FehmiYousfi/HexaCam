#include "RtspAnalyzer.h"
#include "RtspBridge.h"

#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <iomanip>
#include <cstdlib>

static std::atomic<bool> g_quit{false};

static void signalHandler(int /*sig*/) {
    g_quit = true;
}

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <rtsp_uri> [--tcp] [--view] [--duration <seconds>]\n"
              << "       " << prog << " --bridge <config.json>\n"
              << "\n"
              << "Analyzer mode (default):\n"
              << "  <rtsp_uri>           RTSP URI to analyse (required unless --bridge)\n"
              << "  --tcp                Force RTP-over-TCP (interleaved) transport\n"
              << "  --view               Open a window displaying the video stream\n"
              << "  --duration <sec>     Run for <sec> seconds then exit (default: infinite)\n"
              << "\n"
              << "Bridge mode:\n"
              << "  --bridge <config>    Start an RTSP bridge using a JSON config file\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " rtsp://admin:admin@192.168.1.10:554/stream1 --tcp --view --duration 30\n"
              << "  " << prog << " --bridge config/bridge_example.json\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    std::string uri;
    bool use_tcp = false;
    bool view = false;
    int duration_sec = 0;  // 0 = run until Ctrl-C or EOS
    std::string bridge_config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tcp") {
            use_tcp = true;
        } else if (arg == "--view") {
            view = true;
        } else if (arg == "--bridge" && i + 1 < argc) {
            bridge_config_path = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            duration_sec = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (uri.empty()) {
            uri = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Handle Ctrl-C gracefully.
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // -----------------------------------------------------------------------
    // Bridge mode
    // -----------------------------------------------------------------------
    if (!bridge_config_path.empty()) {
        BridgeConfig bcfg;
        if (!parseBridgeConfig(bridge_config_path, bcfg)) {
            std::cerr << "Failed to parse bridge config: " << bridge_config_path << "\n";
            return EXIT_FAILURE;
        }

        RtspBridge bridge;
        if (!bridge.start(bcfg)) {
            std::cerr << "Failed to start RTSP bridge.\n";
            return EXIT_FAILURE;
        }

        // Run the GLib main context so the RTSP server can serve clients.
        GMainContext* ctx = g_main_context_default();
        while (bridge.isRunning() && !g_quit) {
            g_main_context_iteration(ctx, FALSE);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        bridge.stop();
        std::cout << "\n[RtspBridge] Shut down.\n";
        return EXIT_SUCCESS;
    }

    // -----------------------------------------------------------------------
    // Analyzer mode (default)
    // -----------------------------------------------------------------------
    if (uri.empty()) {
        std::cerr << "Error: RTSP URI is required (or use --bridge <config>).\n";
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    RtspAnalyzer analyzer;

    // Register a callback that prints bandwidth stats periodically.
    analyzer.onBandwidthUpdate([](const BandwidthStats& stats) {
        std::cout << std::fixed << std::setprecision(1)
                  << "[BW] current: " << stats.current_kbps << " kbps"
                  << "  avg: " << stats.average_kbps << " kbps"
                  << "  peak: " << stats.peak_kbps << " kbps"
                  << "  total: " << (stats.total_bytes / 1024) << " KB"
                  << "  frames: " << stats.frame_count
                  << "  elapsed: " << stats.elapsed_seconds << " s"
                  << "\r" << std::flush;
    });

    std::cout << "Connecting to: " << uri << "\n";
    std::cout << "Transport    : " << (use_tcp ? "TCP" : "UDP (default)") << "\n";
    std::cout << "Video Window : " << (view ? "enabled" : "disabled") << "\n";
    if (duration_sec > 0) {
        std::cout << "Duration     : " << duration_sec << " seconds\n";
    } else {
        std::cout << "Duration     : until Ctrl-C\n";
    }
    std::cout << std::string(40, '-') << "\n";

    if (!analyzer.start(uri, use_tcp, view)) {
        std::cerr << "Failed to start RTSP analyzer.\n";
        return EXIT_FAILURE;
    }

    // Run the GLib/GStreamer default main context so bus watches fire.
    GMainContext* ctx = g_main_context_default();

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(duration_sec > 0 ? duration_sec : 3600 * 24);

    while (analyzer.isRunning() && !g_quit) {
        // Pump GLib events (non-blocking, 100 ms timeout).
        g_main_context_iteration(ctx, FALSE);

        if (duration_sec > 0 && std::chrono::steady_clock::now() >= deadline) {
            std::cout << "\n[RtspAnalyzer] Duration reached. Stopping.\n";
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    analyzer.stop();

    // Print final summary.
    VideoInfo info = analyzer.getVideoInfo();
    BandwidthStats bw = analyzer.getBandwidthStats();

    std::cout << "\n\n========== Final Report ==========\n";
    std::cout << "  Stream URI   : " << uri << "\n";
    std::cout << "  Codec        : " << info.codec << "\n";
    std::cout << "  Resolution   : " << info.width << "x" << info.height << "\n";
    std::cout << "  Framerate    : " << std::fixed << std::setprecision(2) << info.framerate << " fps\n";
    std::cout << "  Pixel Format : " << info.format << "\n";
    std::cout << "  Transport    : " << info.transport << "\n";
    std::cout << "  --------------------------------\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Avg Bandwidth: " << bw.average_kbps << " kbps ("
              << (bw.average_kbps / 1000.0) << " Mbps)\n";
    std::cout << "  Peak Bandwidth: " << bw.peak_kbps << " kbps ("
              << (bw.peak_kbps / 1000.0) << " Mbps)\n";
    std::cout << "  Total Data   : " << (bw.total_bytes / 1024) << " KB ("
              << (bw.total_bytes / (1024 * 1024)) << " MB)\n";
    std::cout << "  Total Frames : " << bw.frame_count << "\n";
    std::cout << "  Duration     : " << bw.elapsed_seconds << " s\n";
    std::cout << "==================================\n";

    return EXIT_SUCCESS;
}
