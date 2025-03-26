#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <chrono>

#include "screen_capture.h"
#include "websocket_client.h"
#include "input_simulator.h"

class DNFAutoClient {
public:
    DNFAutoClient();
    ~DNFAutoClient();

    bool initialize();
    void run();
    void stop();

private:
    void captureThread();
    void actionThread();
    void processServerResponse(const std::string& response);
    void executeAction(const Action& action);
    void updateGameState();
    std::string getClientInfo();

    struct ClientConfig {
        std::string server_url = "ws://localhost:8080/ws";
        bool verify_ssl = false;
        double capture_interval = 0.5;
        int image_quality = 70;
        std::string window_title = "地下城与勇士";
        int max_retries = 5;
        int retry_delay = 5;
        int heartbeat_interval = 5;

        void load_from_file(const std::string& filename);
    };

    ScreenCapture screen_capture_;
    WebSocketClient ws_client_;
    InputSimulator input_simulator_;

    std::atomic<bool> running_;
    std::mutex action_mutex_;
    std::condition_variable action_cv_;
    std::queue<Action> action_queue_;

    std::thread capture_thread_;
    std::thread action_thread_;

    GameState game_state_;
    ClientConfig config_;
};