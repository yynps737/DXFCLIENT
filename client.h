#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <map>
#include <functional>
#include <random>
#include <nlohmann/json.hpp>
#include "screen_capture.h"
#include "input_simulator.h"
#include "websocket_client.h"

// 客户端状态枚举 - 注意ERROR被重命名为ERROR_STATE以避免与Windows宏冲突
enum class ClientState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ACTIVE,
    PAUSED,
    ERROR_STATE  // 重命名为ERROR_STATE以避免与Windows ERROR宏冲突
};

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

    // 初始化和控制
    bool initialize();
    void run();
    void stop();
    void pause();
    void resume();

    // 获取客户端信息
    std::string getClientInfo();
    ClientState getState() const;
    std::string getStateString() const;

private:
    // 配置结构体
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

    // 状态机相关
    void initializeStateMachine();
    void changeState(ClientState new_state);

    // 状态处理函数
    void handleDisconnectedState();
    void handleConnectingState();
    void handleConnectedState();
    void handleActiveState();
    void handlePausedState();
    void handleErrorState();

    // 线程函数
    void mainLoop();
    void actionThread();
    void statusMonitorThread();

    // 消息处理
    void processServerResponse(const std::string& response);
    void handleActionResponse(const nlohmann::json& data);
    void handleHeartbeatResponse(const nlohmann::json& data);
    void handleErrorResponse(const nlohmann::json& data);

    // 动作执行
    void executeAction(const Action& action);
    void clearActionQueue();

    // 状态更新
    void updateGameState();

    // 配置
    ClientConfig config_;

    // 组件
    ScreenCapture screen_capture_;
    InputSimulator input_simulator_;
    WebSocketClient ws_client_;
    GameState game_state_;

    // 线程
    std::thread main_thread_;
    std::thread action_thread_;
    std::thread status_thread_;
    std::atomic<bool> running_;

    // 状态
    ClientState current_state_;
    std::map<ClientState, std::function<void()>> state_handlers_;

    // 动作队列
    std::queue<Action> action_queue_;
    std::mutex action_mutex_;
    std::condition_variable action_cv_;

    // 状态监控
    std::mutex status_mutex_;
    std::condition_variable status_cv_;

    // 连接管理
    int retry_count_;
    int reconnect_delay_;

    // 性能统计
    int action_counter_;
    int64_t last_action_time_;
    int64_t last_capture_time_;
    int64_t last_heartbeat_time_;
    size_t last_image_hash_;
    int image_change_threshold_;
    int consecutive_errors_;

    // 随机数生成
    std::mt19937 random_engine_;
};