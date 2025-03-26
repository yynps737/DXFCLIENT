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
        logWarn_fmt("�����ӵ�������: {}", url);
        return true;
    }

    logInfo_fmt("���ӵ�������: {}", url);

    // ����ģ�����ӳɹ�
    connected_ = true;
    running_ = true;

    // ����ģ���߳�
    simulation_thread_ = std::thread(&WebSocketClient::simulateServerResponses, this);

    return true;
}

void WebSocketClient::disconnect() {
    std::lock_guard<std::mutex> lock(send_mutex_);

    if (!connected_) {
        return;
    }

    logInfo("�Ͽ�����������");

    // ֹͣģ���߳�
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

    // ����Base64ͼ������
    std::string base64_image = generateBase64Image(jpeg_data);

    // ����JSON��Ϣ
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

    logInfo_fmt("����ͼ�����ݣ���С: {} bytes", jpeg_data.size());

    // ��ʵ��ʵ���У����ǻὫ��Ϣ���͵�������
    // �������ֻ�Ǽ�¼��

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

    // ����������Ϣ
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

    logDebug("��������");

    // ��ʵ��ʵ���У����ǻὫ��Ϣ���͵�������
}

void WebSocketClient::simulateServerResponses() {
    // ģ���߳�
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(5000, 15000);  // ���ʱ���� 5-15 ��

    while (running_) {
        if (connected_ && message_callback_) {
            // ģ���յ���������Ϣ
            std::string response = "{\"type\":\"action_response\",\"actions\":[{\"type\":\"move_random\",\"description\":\"����ƶ�\"}]}";
            message_callback_(response);
        }

        // ����ȴ�һ��ʱ��
        std::this_thread::sleep_for(std::chrono::milliseconds(distrib(gen)));
    }
}

std::string WebSocketClient::generateBase64Image(const std::vector<uint8_t>& jpeg_data) {
    // ʹ��base64����JPEG����
    return base64_encode(jpeg_data.data(), jpeg_data.size());
}