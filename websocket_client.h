#pragma once

// 必须在Windows.h之前包含
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <unordered_map>

// 简化的GameState
struct GameState {
    int player_x = 0;
    int player_y = 0;
    std::string current_map = "";
    float hp_percent = 100.0f;
    float mp_percent = 100.0f;
    bool inventory_full = false;
    std::unordered_map<std::string, float> cooldowns;
};

// WebSocket帧结构
struct WebSocketFrame {
    bool fin = true;
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
};

// WebSocket客户端实现
class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // 连接到WebSocket服务器
    bool connect(const std::string& url, bool verify_ssl = false);

    // 断开连接
    void disconnect();

    // 发送图像和游戏状态
    bool sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state,
        const RECT& window_rect);

    // 设置消息回调函数
    void setMessageCallback(std::function<void(const std::string&)> callback);

    // 检查是否已连接
    bool isConnected() const;

    // 发送心跳消息
    void sendHeartbeat(const GameState& game_state);

private:
    // 解析WebSocket URL
    bool parseUrl(const std::string& url);

    // 创建套接字
    bool createSocket();

    // 连接到服务器
    bool connectToServer();

    // 执行WebSocket握手
    bool performHandshake();

    // 生成WebSocket握手密钥
    std::string generateWebSocketKey();

    // 计算WebSocket握手接受密钥
    std::string calculateAcceptKey(const std::string& websocket_key);

    // 发送WebSocket文本消息
    bool sendTextMessage(const std::string& message);

    // 发送WebSocket二进制消息
    bool sendBinaryMessage(const void* data, size_t length);

    // 发送WebSocket关闭帧
    bool sendCloseFrame();

    // 发送WebSocket Ping帧
    bool sendPingFrame();

    // 发送WebSocket帧
    bool sendWebSocketFrame(uint8_t opcode, const void* data, size_t length);

    // 接收WebSocket消息循环
    void receiveMessages();

    // 解析WebSocket帧
    size_t parseWebSocketFrame(const uint8_t* data, size_t length, WebSocketFrame& frame);

    // 心跳循环
    void heartbeatLoop();

    // 关闭套接字
    void closeSocket();

    // 成员变量
    std::string url_;
    std::string host_;
    std::string path_;
    int port_;
    bool ssl_enabled_;
    bool verify_ssl_;

    std::function<void(const std::string&)> message_callback_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::mutex send_mutex_;
    std::mutex callback_mutex_;
    int request_id_;

    SOCKET websocket_;
    std::thread receiver_thread_;
    std::thread heartbeat_thread_;
};