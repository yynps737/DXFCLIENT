#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include "screen_capture.h"
#include "input_simulator.h"
#include "websocket_client.h"

// 动作结构体定义
struct Action {
    std::string type;              // 动作类型：move_to, click, use_skill 等
    std::string key;               // 按键
    std::vector<std::string> keys; // 按键组合
    std::vector<float> position;   // 位置坐标
    float delay = 0.0f;            // 延迟执行时间（秒）
    std::string purpose;           // 动作目的
    std::string description;       // 动作描述
};

class DNFAutoClient {
public:
    DNFAutoClient();
    ~DNFAutoClient();

    bool initialize();
    void run();
    void stop();
    std::string getClientInfo();

private:
    struct ClientConfig {
        std::string server_url = "ws://localhost:8080";
        bool verify_ssl = false;
        double capture_interval = 0.5;  // 捕获间隔（秒）
        int image_quality = 80;         // 图像质量 (1-100)
        std::string window_title = "地下城与勇士";
        int max_retries = 5;            // 最大重试次数
        int retry_delay = 5;            // 重试延迟（秒）
        int heartbeat_interval = 30;    // 心跳间隔（秒）

        void load_from_file(const std::string& filename);
    };

    void captureThread();
    void actionThread();
    void processServerResponse(const std::string& response);
    void executeAction(const Action& action);
    void updateGameState();

    ClientConfig config_;
    ScreenCapture screen_capture_;
    InputSimulator input_simulator_;
    WebSocketClient ws_client_;
    GameState game_state_;

    std::thread capture_thread_;
    std::thread action_thread_;
    std::atomic<bool> running_;

    std::queue<Action> action_queue_;
    std::mutex action_mutex_;
    std::condition_variable action_cv_;
};