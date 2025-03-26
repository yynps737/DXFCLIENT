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

// WebSocket帧常量
constexpr uint8_t WS_FIN = 0x80;
constexpr uint8_t WS_OPCODE_TEXT = 0x01;
constexpr uint8_t WS_OPCODE_BINARY = 0x02;
constexpr uint8_t WS_OPCODE_CLOSE = 0x08;
constexpr uint8_t WS_OPCODE_PING = 0x09;
constexpr uint8_t WS_OPCODE_PONG = 0x0A;
constexpr uint8_t WS_MASK = 0x80;

// 辅助函数：生成随机UUID
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

// 辅助函数：计算SHA-1哈希
std::string sha1(const std::string& input) {
    // 简易实现，生产环境应使用完整库
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

// 辅助函数：Base64编码
std::string base64Encode(const std::string& input) {
    return base64_encode((const unsigned char*)input.c_str(), input.length());
}

WebSocketClient::WebSocketClient()
    : connected_(false), request_id_(0), running_(false), websocket_(INVALID_SOCKET), ssl_enabled_(false) {
    // 初始化WinSock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        logError_fmt("WSAStartup失败，错误代码: {}", result);
    }
}

WebSocketClient::~WebSocketClient() {
    disconnect();
    WSACleanup();
}

bool WebSocketClient::connect(const std::string& url, bool verify_ssl) {
    if (connected_) {
        logWarn("WebSocket客户端已连接");
        return true;
    }

    try {
        // 解析URL
        if (!parseUrl(url)) {
            logError_fmt("无效的WebSocket URL: {}", url);
            return false;
        }

        // 保存SSL验证设置
        verify_ssl_ = verify_ssl;

        // 创建套接字
        if (!createSocket()) {
            return false;
        }

        // 连接到服务器
        if (!connectToServer()) {
            return false;
        }

        // 执行WebSocket握手
        if (!performHandshake()) {
            closeSocket();
            return false;
        }

        // 启动消息处理线程
        running_ = true;
        receiver_thread_ = std::thread(&WebSocketClient::receiveMessages, this);

        // 启动心跳线程
        heartbeat_thread_ = std::thread(&WebSocketClient::heartbeatLoop, this);

        // 连接成功
        connected_ = true;
        logInfo_fmt("已成功连接到WebSocket服务器: {}", url_);
        return true;
    }
    catch (const std::exception& e) {
        logError_fmt("WebSocket连接错误: {}", e.what());
        closeSocket();
        return false;
    }
}

void WebSocketClient::disconnect() {
    if (!connected_) {
        return;
    }

    // 发送关闭帧
    try {
        sendCloseFrame();
    }
    catch (...) {}

    // 标记为断开连接
    connected_ = false;
    running_ = false;

    // 等待线程结束
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // 关闭套接字
    closeSocket();

    logInfo("WebSocket连接已关闭");
}

bool WebSocketClient::sendImage(const std::vector<uint8_t>& jpeg_data,
                               const GameState& game_state,
                               const RECT& window_rect) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    if (!connected_) {
        logError("WebSocket未连接");
        return false;
    }

    try {
        // 生成Base64编码的图像数据
        std::string base64_image = base64_encode(jpeg_data.data(), jpeg_data.size());

        // 构建JSON消息
        std::ostringstream json;
        json << "{";
        json << "\"type\":\"image\",";
        json << "\"request_id\":" << ++request_id_ << ",";
        json << "\"timestamp\":" << std::time(nullptr) << ",";
        json << "\"data\":\"" << base64_image << "\",";

        // 添加游戏状态
        json << "\"game_state\":{";
        json << "\"player_x\":" << game_state.player_x << ",";
        json << "\"player_y\":" << game_state.player_y << ",";
        json << "\"current_map\":\"" << game_state.current_map << "\",";
        json << "\"hp_percent\":" << game_state.hp_percent << ",";
        json << "\"mp_percent\":" << game_state.mp_percent << ",";
        json << "\"inventory_full\":" << (game_state.inventory_full ? "true" : "false");

        // 添加技能冷却时间
        json << ",\"cooldowns\":{";
        bool first_cooldown = true;
        for (const auto& cooldown : game_state.cooldowns) {
            if (!first_cooldown) json << ",";
            json << "\"" << cooldown.first << "\":" << cooldown.second;
            first_cooldown = false;
        }
        json << "}";

        json << "},";

        // 添加窗口矩形
        json << "\"window_rect\":["
             << window_rect.left << ","
             << window_rect.top << ","
             << window_rect.right << ","
             << window_rect.bottom
             << "]";

        json << "}";

        std::string message = json.str();

        // 发送WebSocket消息
        if (!sendTextMessage(message)) {
            logError("发送图像数据失败");
            return false;
        }

        logDebug_fmt("已发送图像识别请求，图像大小: {:.2f} KB, 请求ID: {}",
                  jpeg_data.size() / 1024.0, request_id_);

        return true;
    }
    catch (const std::exception& e) {
        logError_fmt("发送图像时异常: {}", e.what());
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
        // 构建心跳消息
        std::ostringstream json;
        json << "{";
        json << "\"type\":\"heartbeat\",";
        json << "\"timestamp\":" << std::time(nullptr) << ",";

        // 添加游戏状态
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
            logError("发送心跳消息失败");
            return;
        }

        logDebug("已发送心跳消息");
    }
    catch (const std::exception& e) {
        logError_fmt("发送心跳时异常: {}", e.what());
    }
}

// 私有方法实现
bool WebSocketClient::parseUrl(const std::string& url) {
    std::regex ws_regex("(ws|wss)://([^:/]+)(:\\d+)?(/[^\\s]*)?");
    std::smatch matches;

    if (std::regex_match(url, matches, ws_regex)) {
        // 提取协议
        std::string protocol = matches[1];
        ssl_enabled_ = (protocol == "wss");

        // 提取主机
        host_ = matches[2];

        // 提取端口
        if (matches[3].length() > 0) {
            // 去掉冒号
            std::string port_str = matches[3].str().substr(1);
            port_ = std::stoi(port_str);
        }
        else {
            // 默认端口
            port_ = ssl_enabled_ ? 443 : 80;
        }

        // 提取路径
        if (matches[4].length() > 0) {
            path_ = matches[4];
        }
        else {
            path_ = "/";
        }

        // 保存完整URL
        url_ = url;
        return true;
    }

    return false;
}

bool WebSocketClient::createSocket() {
    // 创建套接字
    websocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (websocket_ == INVALID_SOCKET) {
        logError_fmt("创建套接字失败，错误码: {}", WSAGetLastError());
        return false;
    }

    // 设置超时
    int timeout = 10000; // 10秒
    if (setsockopt(websocket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) != 0) {
        logWarn_fmt("设置接收超时失败，错误码: {}", WSAGetLastError());
    }
    if (setsockopt(websocket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) != 0) {
        logWarn_fmt("设置发送超时失败，错误码: {}", WSAGetLastError());
    }

    // 设置非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(websocket_, FIONBIO, &mode) != 0) {
        logWarn_fmt("设置非阻塞模式失败，错误码: {}", WSAGetLastError());
    }

    return true;
}

bool WebSocketClient::connectToServer() {
    // 解析主机名
    struct addrinfo hints, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // 转换端口号为字符串
    char port_str[10];
    sprintf_s(port_str, "%d", port_);

    // 获取地址信息
    int ret = getaddrinfo(host_.c_str(), port_str, &hints, &result);
    if (ret != 0) {
        logError_fmt("解析主机名失败: {}, 错误码: {}", host_, ret);
        return false;
    }

    // 尝试连接
    bool connected = false;
    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        // 尝试连接
        if (::connect(websocket_, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
                // 非阻塞连接中
                fd_set write_set;
                FD_ZERO(&write_set);
                FD_SET(websocket_, &write_set);

                // 等待连接完成，最多10秒
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
            // 立即连接成功
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);

    if (!connected) {
        logError_fmt("连接到服务器失败: {}:{}", host_, port_);
        return false;
    }

    // 切换回阻塞模式，简化后续操作
    u_long mode = 0;
    if (ioctlsocket(websocket_, FIONBIO, &mode) != 0) {
        logWarn_fmt("切换回阻塞模式失败，错误码: {}", WSAGetLastError());
    }

    return true;
}

bool WebSocketClient::performHandshake() {
    // 生成WebSocket密钥
    std::string websocket_key = generateWebSocketKey();

    // 构建HTTP请求
    std::ostringstream request;
    request << "GET " << path_ << " HTTP/1.1\r\n";
    request << "Host: " << host_ << ":" << port_ << "\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Sec-WebSocket-Key: " << websocket_key << "\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    request << "User-Agent: DNFAutoClient/1.0\r\n";
    request << "\r\n";

    // 发送请求
    std::string request_str = request.str();
    if (send(websocket_, request_str.c_str(), (int)request_str.length(), 0) == SOCKET_ERROR) {
        logError_fmt("发送WebSocket握手请求失败，错误码: {}", WSAGetLastError());
        return false;
    }

    // 接收响应
    char buffer[4096] = {0};
    int bytes_received = recv(websocket_, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        logError_fmt("接收WebSocket握手响应失败，错误码: {}", WSAGetLastError());
        return false;
    }

    // 解析响应
    std::string response(buffer, bytes_received);

    // 检查HTTP状态
    if (response.find("HTTP/1.1 101") == std::string::npos) {
        logError_fmt("WebSocket握手失败，服务器响应: {}", response);
        return false;
    }

    // 验证Sec-WebSocket-Accept
    std::string expected_accept = calculateAcceptKey(websocket_key);
    std::regex accept_regex("Sec-WebSocket-Accept: ([^\r\n]+)");
    std::smatch accept_matches;

    if (std::regex_search(response, accept_matches, accept_regex)) {
        std::string actual_accept = accept_matches[1];
        if (actual_accept != expected_accept) {
            logError_fmt("WebSocket握手安全验证失败");
            return false;
        }
    }
    else {
        logError_fmt("WebSocket握手响应中未找到Accept头");
        return false;
    }

    return true;
}

std::string WebSocketClient::generateWebSocketKey() {
    // 生成16字节随机数
    uint8_t random_bytes[16];
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = rand() % 256;
    }

    // Base64编码
    return base64_encode(random_bytes, 16);
}

std::string WebSocketClient::calculateAcceptKey(const std::string& websocket_key) {
    // 按照RFC6455计算，与固定GUID串连后取SHA-1再Base64
    std::string combined = websocket_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string sha1_hash = sha1(combined);

    // 将SHA-1的十六进制转为二进制
    std::vector<uint8_t> hash_bytes;
    for (size_t i = 0; i < sha1_hash.length(); i += 2) {
        std::string byte_str = sha1_hash.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byte_str.c_str(), NULL, 16);
        hash_bytes.push_back(byte);
    }

    // Base64编码
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
    // 检查连接
    if (websocket_ == INVALID_SOCKET) {
        logError("套接字无效，无法发送WebSocket帧");
        return false;
    }

    // 计算帧大小
    size_t frame_size = 2; // 基本头部

    if (length <= 125) {
        // 7位长度
    }
    else if (length <= 65535) {
        // 16位长度
        frame_size += 2;
    }
    else {
        // 64位长度
        frame_size += 8;
    }

    // 添加掩码数据
    frame_size += 4; // 掩码

    // 添加负载数据
    frame_size += length;

    // 分配帧缓冲区
    std::vector<uint8_t> frame(frame_size, 0);
    size_t pos = 0;

    // FIN + opcode (第一个字节)
    frame[pos++] = WS_FIN | (opcode & 0x0F);

    // MASK + 负载长度 (第二个字节)
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
        // 64位长度 (网络字节序/大端序)
        for (int i = 7; i >= 0; i--) {
            frame[pos++] = (length >> (i * 8)) & 0xFF;
        }
    }

    // 生成随机掩码
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() & 0xFF;
    }

    // 复制掩码
    memcpy(&frame[pos], mask, 4);
    pos += 4;

    // 复制并掩码化数据
    if (data && length > 0) {
        const uint8_t* payload = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; i++) {
            frame[pos++] = payload[i] ^ mask[i % 4];
        }
    }

    // 发送帧
    size_t bytes_sent = 0;
    while (bytes_sent < frame_size) {
        int result = send(websocket_, (const char*)&frame[bytes_sent], (int)(frame_size - bytes_sent), 0);
        if (result == SOCKET_ERROR) {
            logError_fmt("发送WebSocket帧失败，错误码: {}", WSAGetLastError());
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
        // 接收数据
        int bytes_received = recv(websocket_, (char*)frame_buffer.data(), (int)frame_buffer.size(), 0);

        if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // 非阻塞模式下，没有数据可读
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            else if (error == WSAETIMEDOUT) {
                // 超时，继续等待
                continue;
            }
            else {
                // 其他错误
                logError_fmt("接收WebSocket消息失败，错误码: {}", error);
                break;
            }
        }
        else if (bytes_received == 0) {
            // 连接已关闭
            logInfo("WebSocket连接已被服务器关闭");
            break;
        }

        // 处理接收到的数据
        size_t frame_pos = 0;
        while (frame_pos < (size_t)bytes_received) {
            // 解析WebSocket帧
            WebSocketFrame frame;
            size_t frame_bytes = parseWebSocketFrame(frame_buffer.data() + frame_pos, bytes_received - frame_pos, frame);

            if (frame_bytes == 0) {
                // 帧不完整，等待更多数据
                break;
            }

            frame_pos += frame_bytes;

            // 处理帧
            if (frame.opcode == WS_OPCODE_TEXT || frame.opcode == WS_OPCODE_BINARY) {
                // 添加到消息缓冲区
                message_buffer.insert(message_buffer.end(), frame.payload.begin(), frame.payload.end());

                // 如果是最后一个帧，处理完整消息
                if (frame.fin) {
                    // 对于文本消息，添加结束符并回调
                    if (frame.opcode == WS_OPCODE_TEXT) {
                        message_buffer.push_back(0); // 添加字符串结束符
                        std::string message((char*)message_buffer.data());

                        // 调用消息回调
                        std::unique_lock<std::mutex> lock(callback_mutex_);
                        if (message_callback_) {
                            message_callback_(message);
                        }
                    }
                    else {
                        // 二进制消息处理 (暂不支持)
                        logWarn("收到二进制消息，暂不支持处理");
                    }

                    // 清除消息缓冲区
                    message_buffer.clear();
                }
            }
            else if (frame.opcode == WS_OPCODE_CLOSE) {
                // 关闭帧
                logInfo("收到WebSocket关闭帧");
                running_ = false;
                break;
            }
            else if (frame.opcode == WS_OPCODE_PING) {
                // Ping帧，响应Pong
                logDebug("收到Ping，发送Pong");
                sendWebSocketFrame(WS_OPCODE_PONG, frame.payload.data(), frame.payload.size());
            }
            else if (frame.opcode == WS_OPCODE_PONG) {
                // Pong帧，忽略
                logDebug("收到Pong");
            }
        }
    }

    // 如果线程因为错误退出，触发断开连接
    if (running_) {
        running_ = false;
        connected_ = false;
        logInfo("WebSocket连接已断开");
    }
}

size_t WebSocketClient::parseWebSocketFrame(const uint8_t* data, size_t length, WebSocketFrame& frame) {
    if (length < 2) {
        return 0; // 数据不足
    }

    size_t pos = 0;

    // 解析第一个字节
    uint8_t byte1 = data[pos++];
    frame.fin = (byte1 & 0x80) != 0;
    frame.opcode = byte1 & 0x0F;

    // 解析第二个字节
    uint8_t byte2 = data[pos++];
    bool masked = (byte2 & 0x80) != 0;
    uint64_t payload_length = byte2 & 0x7F;

    // 解析扩展长度
    if (payload_length == 126) {
        if (length < pos + 2) {
            return 0; // 数据不足
        }

        payload_length = (data[pos] << 8) | data[pos + 1];
        pos += 2;
    }
    else if (payload_length == 127) {
        if (length < pos + 8) {
            return 0; // 数据不足
        }

        payload_length = 0;
        for (int i = 0; i < 8; i++) {
            payload_length = (payload_length << 8) | data[pos + i];
        }
        pos += 8;
    }

    // 解析掩码
    uint8_t mask[4] = {0};
    if (masked) {
        if (length < pos + 4) {
            return 0; // 数据不足
        }

        memcpy(mask, data + pos, 4);
        pos += 4;
    }

    // 检查数据是否完整
    if (length < pos + payload_length) {
        return 0; // 数据不足
    }

    // 解析负载数据
    frame.payload.resize(payload_length);
    if (masked) {
        // 解掩码
        for (size_t i = 0; i < payload_length; i++) {
            frame.payload[i] = data[pos + i] ^ mask[i % 4];
        }
    }
    else {
        // 不需要解掩码
        memcpy(frame.payload.data(), data + pos, (size_t)payload_length);
    }

    pos += (size_t)payload_length;

    return pos;
}

void WebSocketClient::heartbeatLoop() {
    while (running_) {
        // 每30秒发送一次Ping
        for (int i = 0; i < 30 && running_; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!running_) break;

        // 发送Ping
        logDebug("发送WebSocket Ping");
        if (!sendPingFrame()) {
            logError("发送Ping失败，连接可能已断开");

            // 如果Ping失败，标记连接断开
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