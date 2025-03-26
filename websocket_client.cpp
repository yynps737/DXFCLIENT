#include "websocket_client.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "base64.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <chrono>
#include <ixwebsocket/IXNetSystem.h>  // IXWebSocket ����
#include <ixwebsocket/IXWebSocket.h>  // IXWebSocket

WebSocketClient::WebSocketClient() : connected_(false), request_id_(0) {
    // ��ʼ������ϵͳ
    ix::initNetSystem();  // ������Ҫ��Ϊ ix::IXNetSystem::initialize()
}

WebSocketClient::~WebSocketClient() {
    disconnect();

    // ��������ϵͳ
    ix::uninitNetSystem();  // ������Ҫ��Ϊ ix::IXNetSystem::uninitialize()
}

bool WebSocketClient::connect(const std::string& url, bool verify_ssl) {
    // ����Ƿ��Ѿ�����
    if (connected_) {
        spdlog::warn("WebSocket�ͻ����Ѿ�����");
        return true;
    }

    // ����WebSocketѡ��
    websocket_.setUrl(url);

    // SSL��֤����
    if (!verify_ssl) {
        websocket_.disablePong();
        websocket_.disablePerMessageDeflate();
        websocket_.setTLSOptions({});
    }

    // ������Ϣ�ص�
    websocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        this->onMessage(msg);
        });

    // ��������
    spdlog::info("�������ӵ�������: {}", url);
    websocket_.start();

    // �ȴ����ӽ���
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() - start_time < timeout) {
        if (connected_) {
            spdlog::info("�����ӵ�������");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::error("���ӷ�������ʱ");
    return false;
}

void WebSocketClient::disconnect() {
    if (connected_) {
        spdlog::info("���ڶϿ�����������");
        websocket_.stop();
        connected_ = false;
    }
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state,
    const RECT& window_rect) {
    if (!connected_) {
        spdlog::error("δ���ӵ����������޷�����ͼ��");
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(send_mutex_);

        // ����ͼ���Base64����
        std::string base64_image = generateBase64Image(jpeg_data);

        // ׼��JSON��Ϣ
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();

        // ��Ϣ���ͺ�����ID
        writer.Key("type");
        writer.String("image");
        writer.Key("request_id");
        writer.Int(++request_id_);
        writer.Key("timestamp");
        writer.Double(static_cast<double>(std::chrono::system_clock::now().time_since_epoch().count()) / 1000000000.0);

        // ͼ������
        writer.Key("data");
        writer.String(base64_image.c_str());

        // ���ھ���
        writer.Key("window_rect");
        writer.StartArray();
        writer.Int(window_rect.left);
        writer.Int(window_rect.top);
        writer.Int(window_rect.right);
        writer.Int(window_rect.bottom);
        writer.EndArray();

        // ��Ϸ״̬
        writer.Key("game_state");
        writer.StartObject();

        writer.Key("player_x");
        writer.Int(game_state.player_x);
        writer.Key("player_y");
        writer.Int(game_state.player_y);

        writer.Key("current_map");
        writer.String(game_state.current_map.c_str());

        writer.Key("hp_percent");
        writer.Double(game_state.hp_percent);
        writer.Key("mp_percent");
        writer.Double(game_state.mp_percent);

        writer.Key("inventory_full");
        writer.Bool(game_state.inventory_full);

        // ������ȴ��Ϣ
        writer.Key("cooldowns");
        writer.StartObject();
        for (const auto& cooldown : game_state.cooldowns) {
            writer.Key(cooldown.first.c_str());
            writer.Double(cooldown.second);
        }
        writer.EndObject();

        writer.EndObject(); // game_state

        writer.EndObject(); // root

        // ������Ϣ
        websocket_.send(buffer.GetString());

        return true;
    }
    catch (std::exception& e) {
        spdlog::error("����ͼ��ʱ����: {}", e.what());
        return false;
    }
}

void WebSocketClient::sendHeartbeat(const GameState& game_state) {
    if (!connected_) {
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(send_mutex_);

        // ׼��JSON��Ϣ
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();

        writer.Key("type");
        writer.String("heartbeat");
        writer.Key("timestamp");
        writer.Double(static_cast<double>(std::chrono::system_clock::now().time_since_epoch().count()) / 1000000000.0);

        // ��Ϸ״̬
        writer.Key("game_state");
        writer.StartObject();

        writer.Key("player_x");
        writer.Int(game_state.player_x);
        writer.Key("player_y");
        writer.Int(game_state.player_y);

        writer.Key("hp_percent");
        writer.Double(game_state.hp_percent);
        writer.Key("mp_percent");
        writer.Double(game_state.mp_percent);

        writer.EndObject(); // game_state

        writer.EndObject(); // root

        // ������Ϣ
        websocket_.send(buffer.GetString());
    }
    catch (std::exception& e) {
        spdlog::error("��������ʱ����: {}", e.what());
    }
}

void WebSocketClient::setMessageCallback(std::function<void(const std::string&)> callback) {
    message_callback_ = std::move(callback);
}

bool WebSocketClient::isConnected() const {
    return connected_;
}

void WebSocketClient::onMessage(const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message) {
        // �յ���Ϣ
        const std::string& payload = msg->str;

        // ������Ϣ�ص�
        if (message_callback_) {
            message_callback_(payload);
        }
    }
    else if (msg->type == ix::WebSocketMessageType::Open) {
        // �����Ѵ�
        spdlog::info("WebSocket�����Ѵ�");
        connected_ = true;

        // ������֤��Ӧ
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();
        writer.Key("type");
        writer.String("auth_response");

        writer.Key("client_info");
        writer.StartObject();
        writer.Key("version");
        writer.String("1.0.0");
        writer.Key("platform");
        writer.String("windows");
        writer.EndObject();

        writer.EndObject();

        websocket_.send(buffer.GetString());
    }
    else if (msg->type == ix::WebSocketMessageType::Close) {
        // �����ѹر�
        spdlog::info("WebSocket�����ѹر�: {} - {}", msg->closeInfo.code, msg->closeInfo.reason);
        connected_ = false;
    }
    else if (msg->type == ix::WebSocketMessageType::Error) {
        // ���Ӵ���
        spdlog::error("WebSocket���Ӵ���: {}", msg->errorInfo.reason);
        connected_ = false;
    }
}

std::string WebSocketClient::generateBase64Image(const std::vector<uint8_t>& jpeg_data) {
    // ת��ΪBase64
    return base64_encode(jpeg_data.data(), jpeg_data.size());
}