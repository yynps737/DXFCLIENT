
#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include "ixwebsocket/IXWebSocket.h"

struct GameState {
    int player_x = 0;
    int player_y = 0;
    std::string current_map = "";
    float hp_percent = 100.0f;
    float mp_percent = 100.0f;
    bool inventory_full = false;
    std::unordered_map<std::string, float> cooldowns;
    // ������Ϸ״̬...
};

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    bool connect(const std::string& url, bool verify_ssl = false);
    void disconnect();
    bool sendImage(const std::vector<uint8_t>& jpeg_data, const GameState& game_state,
        const RECT& window_rect);
    void setMessageCallback(std::function<void(const std::string&)> callback);
    bool isConnected() const;
    void sendHeartbeat(const GameState& game_state);

private:
    void onMessage(const ix::WebSocketMessagePtr& msg);
    std::string generateBase64Image(const std::vector<uint8_t>& jpeg_data);

    ix::WebSocket websocket_;
    std::function<void(const std::string&)> message_callback_;
    std::atomic<bool> connected_;
    std::mutex send_mutex_;
    int request_id_;
};