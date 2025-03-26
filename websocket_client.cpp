#include "websocket_client.h"
#include "LogWrapper.h"
#include "base64.h"
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <memory>
#include <regex>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <string>

#include "client.h"
#include "client.h"

#pragma comment(lib, "Ws2_32.lib")

// WebSocket֡����
constexpr uint8_t WS_FIN = 0x80;
constexpr uint8_t WS_OPCODE_TEXT = 0x01;
constexpr uint8_t WS_OPCODE_BINARY = 0x02;
constexpr uint8_t WS_OPCODE_CLOSE = 0x08;
constexpr uint8_t WS_OPCODE_PING = 0x09;
constexpr uint8_t WS_OPCODE_PONG = 0x0A;
constexpr uint8_t WS_MASK = 0x80;

// �����������������UUID
std::string generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* digits = "0123456789abcdef";

    std::string uuid;
    for (int i = 0; i < 32; ++i) {
        uuid += digits[dis(gen)];
        if (i == 7 || i == 11 || i == 15 || i == 19) {
            uuid += '-';
        }
    }
    return uuid;
}

// ��������������SHA-1��ϣ
std::string sha1(const std::string& input) {
    // ����ʵ�֣���������Ӧʹ��������
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hashBuffer[20];
    DWORD hashSize = 20;
    std::string result;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    if (!CryptHashData(hHash, (BYTE*)input.c_str(), (DWORD)input.length(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    if (CryptGetHashParam(hHash, HP_HASHVAL, hashBuffer, &hashSize, 0)) {
        char buf[3];
        for (DWORD i = 0; i < hashSize; i++) {
            sprintf_s(buf, "%02x", hashBuffer[i]);
            result += buf;
        }
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

// ����������Base64����
std::string base64Encode(const std::string& input) {
    return base64_encode((const unsigned char*)input.c_str(), input.length());
}

WebSocketClient::WebSocketClient()
    : connected_(false), request_id_(0), running_(false), websocket_(INVALID_SOCKET), ssl_enabled_(false) {
    // ��ʼ��WinSock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        logError_fmt("WSAStartupʧ�ܣ��������: {}", result);
    }
}

WebSocketClient::~WebSocketClient() {
    disconnect();
    WSACleanup();
}

bool WebSocketClient::connect(const std::string& url, bool verify_ssl) {
    if (connected_) {
        logWarn("WebSocket�ͻ���������");
        return true;
    }

    try {
        // ����URL
        if (!parseUrl(url)) {
            logError_fmt("��Ч��WebSocket URL: {}", url);
            return false;
        }

        // ����SSL��֤����
        verify_ssl_ = verify_ssl;

        // �����׽���
        if (!createSocket()) {
            return false;
        }

        // ���ӵ�������
        if (!connectToServer()) {
            return false;
        }

        // ִ��WebSocket����
        if (!performHandshake()) {
            closeSocket();
            return false;
        }

        // ������Ϣ�����߳�
        running_ = true;
        receiver_thread_ = std::thread(&WebSocketClient::receiveMessages, this);

        // ���������߳�
        heartbeat_thread_ = std::thread(&WebSocketClient::heartbeatLoop, this);

        // ���ӳɹ�
        connected_ = true;
        logInfo_fmt("�ѳɹ����ӵ�WebSocket������: {}", url_);
        return true;
    }
    catch (const std::exception& e) {
        logError_fmt("WebSocket���Ӵ���: {}", e.what());
        closeSocket();
        return false;
    }
}

void WebSocketClient::disconnect() {
    if (!connected_) {
        return;
    }

    // ���͹ر�֡
    try {
        sendCloseFrame();
    }
    catch (...) {}

    // ���Ϊ�Ͽ�����
    connected_ = false;
    running_ = false;

    // �ȴ��߳̽���
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // �ر��׽���
    closeSocket();

    logInfo("WebSocket�����ѹر�");
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data,
                               const GameState& game_state,
                               const RECT& window_rect) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    if (!connected_) {
        logError("WebSocketδ����");
        return false;
    }

    try {
        // ����Base64�����ͼ������
        std::string base64_image = base64_encode(jpeg_data.data(), jpeg_data.size());

        // ����JSON��Ϣ
        std::ostringstream json;
        json << "{";
        json << "\"type\":\"image\",";
        json << "\"request_id\":" << ++request_id_ << ",";
        json << "\"timestamp\":" << std::time(nullptr) << ",";
        json << "\"data\":\"" << base64_image << "\",";

        // �����Ϸ״̬
        json << "\"game_state\":{";
        json << "\"player_x\":" << game_state.player_x << ",";
        json << "\"player_y\":" << game_state.player_y << ",";
        json << "\"current_map\":\"" << game_state.current_map << "\",";
        json << "\"hp_percent\":" << game_state.hp_percent << ",";
        json << "\"mp_percent\":" << game_state.mp_percent << ",";
        json << "\"inventory_full\":" << (game_state.inventory_full ? "true" : "false");

        // ��Ӽ�����ȴʱ��
        json << ",\"cooldowns\":{";
        bool first_cooldown = true;
        for (const auto& cooldown : game_state.cooldowns) {
            if (!first_cooldown) json << ",";
            json << "\"" << cooldown.first << "\":" << cooldown.second;
            first_cooldown = false;
        }
        json << "}";

        json << "},";

        // ��Ӵ��ھ���
        json << "\"window_rect\":["
             << window_rect.left << ","
             << window_rect.top << ","
             << window_rect.right << ","
             << window_rect.bottom
             << "]";

        json << "}";

        std::string message = json.str();

        // ����WebSocket��Ϣ
        if (!sendTextMessage(message)) {
            logError("����ͼ������ʧ��");
            return false;
        }

        logDebug_fmt("�ѷ���ͼ��ʶ������ͼ���С: {:.2f} KB, ����ID: {}",
                  jpeg_data.size() / 1024.0, request_id_);

        return true;
    }
    catch (const std::exception& e) {
        logError_fmt("����ͼ��ʱ�쳣: {}", e.what());
        return false;
    }
}

void WebSocketClient::setMessageCallback(std::function<void(const std::string&)> callback) {
    std::unique_lock<std::mutex> lock(callback_mutex_);
    message_callback_ = callback;
}

bool WebSocketClient::isConnected() const {
    return connected_;
}

void WebSocketClient::sendHeartbeat(const GameState& game_state) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    if (!connected_) {
        return;
    }

    try {
        // ����������Ϣ
        std::ostringstream json;
        json << "{";
        json << "\"type\":\"heartbeat\",";
        json << "\"timestamp\":" << std::time(nullptr) << ",";

        // �����Ϸ״̬
        json << "\"game_state\":{";
        json << "\"player_x\":" << game_state.player_x << ",";
        json << "\"player_y\":" << game_state.player_y << ",";
        json << "\"current_map\":\"" << game_state.current_map << "\",";
        json << "\"hp_percent\":" << game_state.hp_percent << ",";
        json << "\"mp_percent\":" << game_state.mp_percent << ",";
        json << "\"inventory_full\":" << (game_state.inventory_full ? "true" : "false");
        json << "}";

        json << "}";

        std::string message = json.str();

        if (!sendTextMessage(message)) {
            logError("����������Ϣʧ��");
            return;
        }

        logDebug("�ѷ���������Ϣ");
    }
    catch (const std::exception& e) {
        logError_fmt("��������ʱ�쳣: {}", e.what());
    }
}

// ˽�з���ʵ��
bool WebSocketClient::parseUrl(const std::string& url) {
    std::regex ws_regex("(ws|wss)://([^:/]+)(:\\d+)?(/[^\\s]*)?");
    std::smatch matches;

    if (std::regex_match(url, matches, ws_regex)) {
        // ��ȡЭ��
        std::string protocol = matches[1];
        ssl_enabled_ = (protocol == "wss");

        // ��ȡ����
        host_ = matches[2];

        // ��ȡ�˿�
        if (matches[3].length() > 0) {
            // ȥ��ð��
            std::string port_str = matches[3].str().substr(1);
            port_ = std::stoi(port_str);
        }
        else {
            // Ĭ�϶˿�
            port_ = ssl_enabled_ ? 443 : 80;
        }

        // ��ȡ·��
        if (matches[4].length() > 0) {
            path_ = matches[4];
        }
        else {
            path_ = "/";
        }

        // ��������URL
        url_ = url;
        return true;
    }

    return false;
}

bool WebSocketClient::createSocket() {
    // �����׽���
    websocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (websocket_ == INVALID_SOCKET) {
        logError_fmt("�����׽���ʧ�ܣ�������: {}", WSAGetLastError());
        return false;
    }

    // ���ó�ʱ
    int timeout = 10000; // 10��
    if (setsockopt(websocket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) != 0) {
        logWarn_fmt("���ý��ճ�ʱʧ�ܣ�������: {}", WSAGetLastError());
    }
    if (setsockopt(websocket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) != 0) {
        logWarn_fmt("���÷��ͳ�ʱʧ�ܣ�������: {}", WSAGetLastError());
    }

    // ���÷�����ģʽ
    u_long mode = 1;
    if (ioctlsocket(websocket_, FIONBIO, &mode) != 0) {
        logWarn_fmt("���÷�����ģʽʧ�ܣ�������: {}", WSAGetLastError());
    }

    return true;
}

bool WebSocketClient::connectToServer() {
    // ����������
    struct addrinfo hints, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // ת���˿ں�Ϊ�ַ���
    char port_str[10];
    sprintf_s(port_str, "%d", port_);

    // ��ȡ��ַ��Ϣ
    int ret = getaddrinfo(host_.c_str(), port_str, &hints, &result);
    if (ret != 0) {
        logError_fmt("����������ʧ��: {}, ������: {}", host_, ret);
        return false;
    }

    // ��������
    bool connected = false;
    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        // ��������
        if (::connect(websocket_, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
                // ������������
                fd_set write_set;
                FD_ZERO(&write_set);
                FD_SET(websocket_, &write_set);

                // �ȴ�������ɣ����10��
                struct timeval tv;
                tv.tv_sec = 10;
                tv.tv_usec = 0;

                if (select(0, NULL, &write_set, NULL, &tv) > 0) {
                    connected = true;
                    break;
                }
            }
        }
        else {
            // �������ӳɹ�
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);

    if (!connected) {
        logError_fmt("���ӵ�������ʧ��: {}:{}", host_, port_);
        return false;
    }

    // �л�������ģʽ���򻯺�������
    u_long mode = 0;
    if (ioctlsocket(websocket_, FIONBIO, &mode) != 0) {
        logWarn_fmt("�л�������ģʽʧ�ܣ�������: {}", WSAGetLastError());
    }

    return true;
}

bool WebSocketClient::performHandshake() {
    // ����WebSocket��Կ
    std::string websocket_key = generateWebSocketKey();

    // ����HTTP����
    std::ostringstream request;
    request << "GET " << path_ << " HTTP/1.1\r\n";
    request << "Host: " << host_ << ":" << port_ << "\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Sec-WebSocket-Key: " << websocket_key << "\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    request << "User-Agent: DNFAutoClient/1.0\r\n";
    request << "\r\n";

    // ��������
    std::string request_str = request.str();
    if (send(websocket_, request_str.c_str(), (int)request_str.length(), 0) == SOCKET_ERROR) {
        logError_fmt("����WebSocket��������ʧ�ܣ�������: {}", WSAGetLastError());
        return false;
    }

    // ������Ӧ
    char buffer[4096] = {0};
    int bytes_received = recv(websocket_, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        logError_fmt("����WebSocket������Ӧʧ�ܣ�������: {}", WSAGetLastError());
        return false;
    }

    // ������Ӧ
    std::string response(buffer, bytes_received);

    // ���HTTP״̬
    if (response.find("HTTP/1.1 101") == std::string::npos) {
        logError_fmt("WebSocket����ʧ�ܣ���������Ӧ: {}", response);
        return false;
    }

    // ��֤Sec-WebSocket-Accept
    std::string expected_accept = calculateAcceptKey(websocket_key);
    std::regex accept_regex("Sec-WebSocket-Accept: ([^\r\n]+)");
    std::smatch accept_matches;

    if (std::regex_search(response, accept_matches, accept_regex)) {
        std::string actual_accept = accept_matches[1];
        if (actual_accept != expected_accept) {
            logError_fmt("WebSocket���ְ�ȫ��֤ʧ��");
            return false;
        }
    }
    else {
        logError_fmt("WebSocket������Ӧ��δ�ҵ�Acceptͷ");
        return false;
    }

    return true;
}

std::string WebSocketClient::generateWebSocketKey() {
    // ����16�ֽ������
    uint8_t random_bytes[16];
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = rand() % 256;
    }

    // Base64����
    return base64_encode(random_bytes, 16);
}

std::string WebSocketClient::calculateAcceptKey(const std::string& websocket_key) {
    // ����RFC6455���㣬��̶�GUID������ȡSHA-1��Base64
    std::string combined = websocket_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string sha1_hash = sha1(combined);

    // ��SHA-1��ʮ������תΪ������
    std::vector<uint8_t> hash_bytes;
    for (size_t i = 0; i < sha1_hash.length(); i += 2) {
        std::string byte_str = sha1_hash.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byte_str.c_str(), NULL, 16);
        hash_bytes.push_back(byte);
    }

    // Base64����
    return base64_encode(hash_bytes.data(), hash_bytes.size());
}

bool WebSocketClient::sendTextMessage(const std::string& message) {
    return sendWebSocketFrame(WS_OPCODE_TEXT, message.data(), message.size());
}

bool WebSocketClient::sendBinaryMessage(const void* data, size_t length) {
    return sendWebSocketFrame(WS_OPCODE_BINARY, data, length);
}

bool WebSocketClient::sendCloseFrame() {
    return sendWebSocketFrame(WS_OPCODE_CLOSE, nullptr, 0);
}

bool WebSocketClient::sendPingFrame() {
    return sendWebSocketFrame(WS_OPCODE_PING, nullptr, 0);
}

bool WebSocketClient::sendWebSocketFrame(uint8_t opcode, const void* data, size_t length) {
    // �������
    if (websocket_ == INVALID_SOCKET) {
        logError("�׽�����Ч���޷�����WebSocket֡");
        return false;
    }

    // ����֡��С
    size_t frame_size = 2; // ����ͷ��

    if (length <= 125) {
        // 7λ����
    }
    else if (length <= 65535) {
        // 16λ����
        frame_size += 2;
    }
    else {
        // 64λ����
        frame_size += 8;
    }

    // �����������
    frame_size += 4; // ����

    // ��Ӹ�������
    frame_size += length;

    // ����֡������
    std::vector<uint8_t> frame(frame_size, 0);
    size_t pos = 0;

    // FIN + opcode (��һ���ֽ�)
    frame[pos++] = WS_FIN | (opcode & 0x0F);

    // MASK + ���س��� (�ڶ����ֽ�)
    if (length <= 125) {
        frame[pos++] = WS_MASK | (uint8_t)length;
    }
    else if (length <= 65535) {
        frame[pos++] = WS_MASK | 126;
        frame[pos++] = (length >> 8) & 0xFF;
        frame[pos++] = length & 0xFF;
    }
    else {
        frame[pos++] = WS_MASK | 127;
        // 64λ���� (�����ֽ���/�����)
        for (int i = 7; i >= 0; i--) {
            frame[pos++] = (length >> (i * 8)) & 0xFF;
        }
    }

    // �����������
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() & 0xFF;
    }

    // ��������
    memcpy(&frame[pos], mask, 4);
    pos += 4;

    // ���Ʋ����뻯����
    if (data && length > 0) {
        const uint8_t* payload = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; i++) {
            frame[pos++] = payload[i] ^ mask[i % 4];
        }
    }

    // ����֡
    size_t bytes_sent = 0;
    while (bytes_sent < frame_size) {
        int result = send(websocket_, (const char*)&frame[bytes_sent], (int)(frame_size - bytes_sent), 0);
        if (result == SOCKET_ERROR) {
            logError_fmt("����WebSocket֡ʧ�ܣ�������: {}", WSAGetLastError());
            return false;
        }

        bytes_sent += result;
    }

    return true;
}

void WebSocketClient::receiveMessages() {
    std::vector<uint8_t> message_buffer;
    std::vector<uint8_t> frame_buffer(4096);

    while (running_) {
        // ��������
        int bytes_received = recv(websocket_, (char*)frame_buffer.data(), (int)frame_buffer.size(), 0);

        if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // ������ģʽ�£�û�����ݿɶ�
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            else if (error == WSAETIMEDOUT) {
                // ��ʱ�������ȴ�
                continue;
            }
            else {
                // ��������
                logError_fmt("����WebSocket��Ϣʧ�ܣ�������: {}", error);
                break;
            }
        }
        else if (bytes_received == 0) {
            // �����ѹر�
            logInfo("WebSocket�����ѱ��������ر�");
            break;
        }

        // ������յ�������
        size_t frame_pos = 0;
        while (frame_pos < (size_t)bytes_received) {
            // ����WebSocket֡
            WebSocketFrame frame;
            size_t frame_bytes = parseWebSocketFrame(frame_buffer.data() + frame_pos, bytes_received - frame_pos, frame);

            if (frame_bytes == 0) {
                // ֡���������ȴ���������
                break;
            }

            frame_pos += frame_bytes;

            // ����֡
            if (frame.opcode == WS_OPCODE_TEXT || frame.opcode == WS_OPCODE_BINARY) {
                // ��ӵ���Ϣ������
                message_buffer.insert(message_buffer.end(), frame.payload.begin(), frame.payload.end());

                // ��������һ��֡������������Ϣ
                if (frame.fin) {
                    // �����ı���Ϣ����ӽ��������ص�
                    if (frame.opcode == WS_OPCODE_TEXT) {
                        message_buffer.push_back(0); // ����ַ���������
                        std::string message((char*)message_buffer.data());

                        // ������Ϣ�ص�
                        std::unique_lock<std::mutex> lock(callback_mutex_);
                        if (message_callback_) {
                            message_callback_(message);
                        }
                    }
                    else {
                        // ��������Ϣ���� (�ݲ�֧��)
                        logWarn("�յ���������Ϣ���ݲ�֧�ִ���");
                    }

                    // �����Ϣ������
                    message_buffer.clear();
                }
            }
            else if (frame.opcode == WS_OPCODE_CLOSE) {
                // �ر�֡
                logInfo("�յ�WebSocket�ر�֡");
                running_ = false;
                break;
            }
            else if (frame.opcode == WS_OPCODE_PING) {
                // Ping֡����ӦPong
                logDebug("�յ�Ping������Pong");
                sendWebSocketFrame(WS_OPCODE_PONG, frame.payload.data(), frame.payload.size());
            }
            else if (frame.opcode == WS_OPCODE_PONG) {
                // Pong֡������
                logDebug("�յ�Pong");
            }
        }
    }

    // ����߳���Ϊ�����˳��������Ͽ�����
    if (running_) {
        running_ = false;
        connected_ = false;
        logInfo("WebSocket�����ѶϿ�");
    }
}

size_t WebSocketClient::parseWebSocketFrame(const uint8_t* data, size_t length, WebSocketFrame& frame) {
    if (length < 2) {
        return 0; // ���ݲ���
    }

    size_t pos = 0;

    // ������һ���ֽ�
    uint8_t byte1 = data[pos++];
    frame.fin = (byte1 & 0x80) != 0;
    frame.opcode = byte1 & 0x0F;

    // �����ڶ����ֽ�
    uint8_t byte2 = data[pos++];
    bool masked = (byte2 & 0x80) != 0;
    uint64_t payload_length = byte2 & 0x7F;

    // ������չ����
    if (payload_length == 126) {
        if (length < pos + 2) {
            return 0; // ���ݲ���
        }

        payload_length = (data[pos] << 8) | data[pos + 1];
        pos += 2;
    }
    else if (payload_length == 127) {
        if (length < pos + 8) {
            return 0; // ���ݲ���
        }

        payload_length = 0;
        for (int i = 0; i < 8; i++) {
            payload_length = (payload_length << 8) | data[pos + i];
        }
        pos += 8;
    }

    // ��������
    uint8_t mask[4] = {0};
    if (masked) {
        if (length < pos + 4) {
            return 0; // ���ݲ���
        }

        memcpy(mask, data + pos, 4);
        pos += 4;
    }

    // ��������Ƿ�����
    if (length < pos + payload_length) {
        return 0; // ���ݲ���
    }

    // ������������
    frame.payload.resize(payload_length);
    if (masked) {
        // ������
        for (size_t i = 0; i < payload_length; i++) {
            frame.payload[i] = data[pos + i] ^ mask[i % 4];
        }
    }
    else {
        // ����Ҫ������
        memcpy(frame.payload.data(), data + pos, (size_t)payload_length);
    }

    pos += (size_t)payload_length;

    return pos;
}

void WebSocketClient::heartbeatLoop() {
    while (running_) {
        // ÿ30�뷢��һ��Ping
        for (int i = 0; i < 30 && running_; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!running_) break;

        // ����Ping
        logDebug("����WebSocket Ping");
        if (!sendPingFrame()) {
            logError("����Pingʧ�ܣ����ӿ����ѶϿ�");

            // ���Pingʧ�ܣ�������ӶϿ�
            running_ = false;
            connected_ = false;
            break;
        }
    }
}

void WebSocketClient::closeSocket() {
    if (websocket_ != INVALID_SOCKET) {
        closesocket(websocket_);
        websocket_ = INVALID_SOCKET;
    }
}