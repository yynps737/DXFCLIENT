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
    // ����WebSocket���� (ģ��)
    logInfo_fmt("�������ӵ�: {}", url);
    
    // ģ�����ӳɹ�
    connected_ = true;
    
    // ����ģ���������Ӧ���߳�
    running_ = true;
    simulation_thread_ = std::thread(&WebSocketClient::simulateServerResponses, this);
    
    logInfo("WebSocket�����ѽ��� (ģ��)");
    return true;
}

void WebSocketClient::disconnect() {
    if (connected_) {
        // ֹͣģ���߳�
        running_ = false;
        if (simulation_thread_.joinable()) {
            simulation_thread_.join();
        }
        
        connected_ = false;
        logInfo("WebSocket�����ѶϿ� (ģ��)");
    }
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state, const RECT& window_rect) {
    if (!connected_) {
        logError("WebSocketδ���ӣ��޷�����ͼ��");
        return false;
    }
    
    try {
        // ����Base64�����ͼ��
        std::string base64_image = generateBase64Image(jpeg_data);
        
        // ����JSON��Ϣ (��)
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
        
        // ģ�ⷢ�ͳɹ�
        logDebug_fmt("�ѷ���ͼ����� ({}x{}), ��С: {} bytes", 
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            jpeg_data.size());
        
        return true;
    }
    catch (const std::exception& e) {
        logError_fmt("����ͼ��ʱ�����쳣: {}", e.what());
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
        // ����������Ϣ (��)
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
        
        // ģ�ⷢ�ͳɹ�
        logDebug("�ѷ���������Ϣ (ģ��)");
    }
    catch (const std::exception& e) {
        logError_fmt("����������Ϣʱ�����쳣: {}", e.what());
    }
}

void WebSocketClient::simulateServerResponses() {
    // ģ���������Ӧ
    int counter = 0;
    
    while (running_ && connected_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (message_callback_) {
            counter++;
            
            if (counter % 3 == 0) {
                // ÿ15�뷢��һ��������Ӧ
                std::ostringstream os;
                os << "{"
                   << "\"type\":\"action_response\","
                   << "\"request_id\":" << request_id_ << ","
                   << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count() << ","
                   << "\"actions\":[{"
                   << "\"type\":\"move_random\","
                   << "\"description\":\"����ƶ� (�Զ�)\""
                   << "}]"
                   << "}";
                
                message_callback_(os.str());
            } else {
                // ����������Ӧ
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
    // ʹ�����ǵ�base64���뺯��
    return base64_encode(jpeg_data.data(), jpeg_data.size());
}