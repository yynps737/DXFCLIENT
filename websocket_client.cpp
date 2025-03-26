#include "websocket_client.h"
#include "LogWrapper.h"
#include "base64.h"
#include <sstream>
#include <random>
#include <chrono>
#include <thread>

WebSocketClient::WebSocketClient() : connected_(false), running_(false), request_id_(0) {
}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

bool WebSocketClient::connect(const std::string& url, bool verify_ssl) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    if (connected_) {
        logWarn_fmt("已连接到服务器: {}", url);
        return true;
    }

    logInfo_fmt("连接到服务器: {}", url);

    // 这里模拟连接成功
    connected_ = true;
    running_ = true;

    // 启动模拟线程
    simulation_thread_ = std::thread(&WebSocketClient::simulateServerResponses, this);

    return true;
}

void WebSocketClient::disconnect() {
    std::lock_guard<std::mutex> lock(send_mutex_);

    if (!connected_) {
        return;
    }

    logInfo("断开服务器连接");

    // 停止模拟线程
    running_ = false;
    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }

    connected_ = false;
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state, const RECT& window_rect) {
    if (!connected_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);

    // 生成Base64图像数据
    std::string base64_image = generateBase64Image(jpeg_data);

    // 构造JSON消息
    std::ostringstream os;
    os << "{"
       << "\"type\":\"image\","
       << "\"id\":" << ++request_id_ << ","
       << "\"image\":\"" << base64_image << "\","
       << "\"game_state\":{"
       << "\"player_x\":" << game_state.player_x << ","
       << "\"player_y\":" << game_state.player_y << ","
       << "\"current_map\":\"" << game_state.current_map << "\","
       << "\"hp_percent\":" << game_state.hp_percent << ","
       << "\"mp_percent\":" << game_state.mp_percent << ","
       << "\"inventory_full\":" << (game_state.inventory_full ? "true" : "false")
       << "},"
       << "\"window_rect\":{"
       << "\"left\":" << window_rect.left << ","
       << "\"top\":" << window_rect.top << ","
       << "\"right\":" << window_rect.right << ","
       << "\"bottom\":" << window_rect.bottom
       << "}"
       << "}";

    logInfo_fmt("发送图像数据，大小: {} bytes", jpeg_data.size());

    // 在实际实现中，我们会将消息发送到服务器
    // 这里，我们只是记录它

    return true;
}

void WebSocketClient::setMessageCallback(std::function<void(const std::string&)> callback) {
    message_callback_ = callback;
}

bool WebSocketClient::isConnected() const {
    return connected_;
}

void WebSocketClient::sendHeartbeat(const GameState& game_state) {
    if (!connected_) {
        return;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);

    // 构造心跳消息
    std::ostringstream os;
    os << "{"
       << "\"type\":\"heartbeat\","
       << "\"id\":" << ++request_id_ << ","
       << "\"game_state\":{"
       << "\"player_x\":" << game_state.player_x << ","
       << "\"player_y\":" << game_state.player_y << ","
       << "\"current_map\":\"" << game_state.current_map << "\","
       << "\"hp_percent\":" << game_state.hp_percent << ","
       << "\"mp_percent\":" << game_state.mp_percent << ","
       << "\"inventory_full\":" << (game_state.inventory_full ? "true" : "false")
       << "}"
       << "}";

    logDebug("发送心跳");

    // 在实际实现中，我们会将消息发送到服务器
}

void WebSocketClient::simulateServerResponses() {
    // 模拟线程
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(5000, 15000);  // 随机时间间隔 5-15 秒

    while (running_) {
        if (connected_ && message_callback_) {
            // 模拟收到服务器消息
            std::string response = "{\"type\":\"action_response\",\"actions\":[{\"type\":\"move_random\",\"description\":\"随机移动\"}]}";
            message_callback_(response);
        }

        // 随机等待一段时间
        std::this_thread::sleep_for(std::chrono::milliseconds(distrib(gen)));
    }
}

std::string WebSocketClient::generateBase64Image(const std::vector<uint8_t>& jpeg_data) {
    // 使用base64编码JPEG数据
    return base64_encode(jpeg_data.data(), jpeg_data.size());
}