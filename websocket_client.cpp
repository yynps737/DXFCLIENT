#include "websocket_client.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "base64.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <chrono>
#include <ixwebsocket/IXNetSystem.h>  // IXWebSocket 网络
#include <ixwebsocket/IXWebSocket.h>  // IXWebSocket

WebSocketClient::WebSocketClient() : connected_(false), request_id_(0) {
    // 初始化网络系统
    ix::initNetSystem();  // 可能需要改为 ix::IXNetSystem::initialize()
}

WebSocketClient::~WebSocketClient() {
    disconnect();

    // 清理网络系统
    ix::uninitNetSystem();  // 可能需要改为 ix::IXNetSystem::uninitialize()
}

bool WebSocketClient::connect(const std::string& url, bool verify_ssl) {
    // 检查是否已经连接
    if (connected_) {
        spdlog::warn("WebSocket客户端已经连接");
        return true;
    }

    // 设置WebSocket选项
    websocket_.setUrl(url);

    // SSL验证设置
    if (!verify_ssl) {
        websocket_.disablePong();
        websocket_.disablePerMessageDeflate();
        websocket_.setTLSOptions({});
    }

    // 设置消息回调
    websocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        this->onMessage(msg);
        });

    // 启动连接
    spdlog::info("正在连接到服务器: {}", url);
    websocket_.start();

    // 等待连接建立
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() - start_time < timeout) {
        if (connected_) {
            spdlog::info("已连接到服务器");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::error("连接服务器超时");
    return false;
}

void WebSocketClient::disconnect() {
    if (connected_) {
        spdlog::info("正在断开服务器连接");
        websocket_.stop();
        connected_ = false;
    }
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state,
    const RECT& window_rect) {
    if (!connected_) {
        spdlog::error("未连接到服务器，无法发送图像");
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(send_mutex_);

        // 生成图像的Base64编码
        std::string base64_image = generateBase64Image(jpeg_data);

        // 准备JSON消息
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();

        // 消息类型和请求ID
        writer.Key("type");
        writer.String("image");
        writer.Key("request_id");
        writer.Int(++request_id_);
        writer.Key("timestamp");
        writer.Double(static_cast<double>(std::chrono::system_clock::now().time_since_epoch().count()) / 1000000000.0);

        // 图像数据
        writer.Key("data");
        writer.String(base64_image.c_str());

        // 窗口矩形
        writer.Key("window_rect");
        writer.StartArray();
        writer.Int(window_rect.left);
        writer.Int(window_rect.top);
        writer.Int(window_rect.right);
        writer.Int(window_rect.bottom);
        writer.EndArray();

        // 游戏状态
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

        // 技能冷却信息
        writer.Key("cooldowns");
        writer.StartObject();
        for (const auto& cooldown : game_state.cooldowns) {
            writer.Key(cooldown.first.c_str());
            writer.Double(cooldown.second);
        }
        writer.EndObject();

        writer.EndObject(); // game_state

        writer.EndObject(); // root

        // 发送消息
        websocket_.send(buffer.GetString());

        return true;
    }
    catch (std::exception& e) {
        spdlog::error("发送图像时出错: {}", e.what());
        return false;
    }
}

void WebSocketClient::sendHeartbeat(const GameState& game_state) {
    if (!connected_) {
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(send_mutex_);

        // 准备JSON消息
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        writer.StartObject();

        writer.Key("type");
        writer.String("heartbeat");
        writer.Key("timestamp");
        writer.Double(static_cast<double>(std::chrono::system_clock::now().time_since_epoch().count()) / 1000000000.0);

        // 游戏状态
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

        // 发送消息
        websocket_.send(buffer.GetString());
    }
    catch (std::exception& e) {
        spdlog::error("发送心跳时出错: {}", e.what());
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
        // 收到消息
        const std::string& payload = msg->str;

        // 调用消息回调
        if (message_callback_) {
            message_callback_(payload);
        }
    }
    else if (msg->type == ix::WebSocketMessageType::Open) {
        // 连接已打开
        spdlog::info("WebSocket连接已打开");
        connected_ = true;

        // 发送认证响应
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
        // 连接已关闭
        spdlog::info("WebSocket连接已关闭: {} - {}", msg->closeInfo.code, msg->closeInfo.reason);
        connected_ = false;
    }
    else if (msg->type == ix::WebSocketMessageType::Error) {
        // 连接错误
        spdlog::error("WebSocket连接错误: {}", msg->errorInfo.reason);
        connected_ = false;
    }
}

std::string WebSocketClient::generateBase64Image(const std::vector<uint8_t>& jpeg_data) {
    // 转换为Base64
    return base64_encode(jpeg_data.data(), jpeg_data.size());
}