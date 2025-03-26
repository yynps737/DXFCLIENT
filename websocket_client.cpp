#include "websocket_client.h"
#include "LogWrapper.h"
#include "base64.h"
#include <sstream>
#include <thread>
#include <chrono>

WebSocketClient::WebSocketClient() : connected_(false), request_id_(0), running_(false) {
}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

bool WebSocketClient::connect(const std::string& url, bool verify_ssl) {
    // 配置WebSocket连接 (模拟)
    logInfo_fmt("尝试连接到: {}", url);
    
    // 模拟连接成功
    connected_ = true;
    
    // 启动模拟服务器响应的线程
    running_ = true;
    simulation_thread_ = std::thread(&WebSocketClient::simulateServerResponses, this);
    
    logInfo("WebSocket连接已建立 (模拟)");
    return true;
}

void WebSocketClient::disconnect() {
    if (connected_) {
        // 停止模拟线程
        running_ = false;
        if (simulation_thread_.joinable()) {
            simulation_thread_.join();
        }
        
        connected_ = false;
        logInfo("WebSocket连接已断开 (模拟)");
    }
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state, const RECT& window_rect) {
    if (!connected_) {
        logError("WebSocket未连接，无法发送图像");
        return false;
    }
    
    try {
        // 生成Base64编码的图像
        std::string base64_image = generateBase64Image(jpeg_data);
        
        // 构建JSON消息 (简化)
        std::ostringstream os;
        os << "{"
           << "\"type\":\"image_update\","
           << "\"request_id\":" << ++request_id_ << ","
           << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count() << ","
           << "\"image_size\":" << base64_image.size() << ","
           << "\"game_state\":{"
           << "\"player_x\":" << game_state.player_x << ","
           << "\"player_y\":" << game_state.player_y << ","
           << "\"hp_percent\":" << game_state.hp_percent << ","
           << "\"mp_percent\":" << game_state.mp_percent
           << "}"
           << "}";
        
        // 模拟发送成功
        logDebug_fmt("已发送图像更新 ({}x{}), 大小: {} bytes", 
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            jpeg_data.size());
        
        return true;
    }
    catch (const std::exception& e) {
        logError_fmt("发送图像时发生异常: {}", e.what());
        return false;
    }
}

void WebSocketClient::setMessageCallback(std::function<void(const std::string&)> callback) {
    message_callback_ = std::move(callback);
}

bool WebSocketClient::isConnected() const {
    return connected_;
}

void WebSocketClient::sendHeartbeat(const GameState& game_state) {
    if (!connected_) {
        return;
    }
    
    try {
        // 构建心跳消息 (简化)
        std::ostringstream os;
        os << "{"
           << "\"type\":\"heartbeat\","
           << "\"request_id\":" << ++request_id_ << ","
           << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count() << ","
           << "\"game_state\":{"
           << "\"player_x\":" << game_state.player_x << ","
           << "\"player_y\":" << game_state.player_y << ","
           << "\"hp_percent\":" << game_state.hp_percent << ","
           << "\"mp_percent\":" << game_state.mp_percent
           << "}"
           << "}";
        
        // 模拟发送成功
        logDebug("已发送心跳消息 (模拟)");
    }
    catch (const std::exception& e) {
        logError_fmt("发送心跳消息时发生异常: {}", e.what());
    }
}

void WebSocketClient::simulateServerResponses() {
    // 模拟服务器响应
    int counter = 0;
    
    while (running_ && connected_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (message_callback_) {
            counter++;
            
            if (counter % 3 == 0) {
                // 每15秒发送一个动作响应
                std::ostringstream os;
                os << "{"
                   << "\"type\":\"action_response\","
                   << "\"request_id\":" << request_id_ << ","
                   << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count() << ","
                   << "\"actions\":[{"
                   << "\"type\":\"move_random\","
                   << "\"description\":\"随机移动 (自动)\""
                   << "}]"
                   << "}";
                
                message_callback_(os.str());
            } else {
                // 发送心跳响应
                std::ostringstream os;
                os << "{"
                   << "\"type\":\"heartbeat_response\","
                   << "\"request_id\":" << request_id_ << ","
                   << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count()
                   << "}";
                
                message_callback_(os.str());
            }
        }
    }
}

std::string WebSocketClient::generateBase64Image(const std::vector<uint8_t>& jpeg_data) {
    // 使用我们的base64编码函数
    return base64_encode(jpeg_data.data(), jpeg_data.size());
}