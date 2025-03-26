#include "client.h"
#include "LogWrapper.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <functional>
#include <memory>
#include <regex>
#include <mutex>
#include <atomic>
#include <random>
#include <nlohmann/json.hpp>

// 使用nlohmann-json库进行JSON处理
using json = nlohmann::json;

// 配置默认值
const std::string DEFAULT_SERVER_URL = "ws://localhost:8080";
const std::string DEFAULT_WINDOW_TITLE = "地下城与勇士";
const double DEFAULT_CAPTURE_INTERVAL = 0.5;
const int DEFAULT_IMAGE_QUALITY = 80;

// 状态常量
enum class ClientState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ACTIVE,
    PAUSED,
    ERROR
};

// 状态到字符串的映射
const std::map<ClientState, std::string> STATE_NAMES = {
    {ClientState::DISCONNECTED, "断开连接"},
    {ClientState::CONNECTING, "正在连接"},
    {ClientState::CONNECTED, "已连接"},
    {ClientState::ACTIVE, "运行中"},
    {ClientState::PAUSED, "已暂停"},
    {ClientState::ERROR, "错误"}
};

DNFAutoClient::DNFAutoClient()
    : current_state_(ClientState::DISCONNECTED),
      running_(false),
      retry_count_(0),
      reconnect_delay_(0),
      action_counter_(0),
      last_action_time_(0),
      last_capture_time_(0),
      last_image_hash_(0),
      image_change_threshold_(5000),
      consecutive_errors_(0) {

    // 加载配置
    config_.load_from_file("config.ini");

    // 初始化随机数生成器
    std::random_device rd;
    random_engine_ = std::mt19937(rd());
}

DNFAutoClient::~DNFAutoClient() {
    stop();
}

void DNFAutoClient::ClientConfig::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        logWarn_fmt("无法打开配置文件: {}，使用默认值", filename);

        // 设置默认值
        server_url = DEFAULT_SERVER_URL;
        window_title = DEFAULT_WINDOW_TITLE;
        capture_interval = DEFAULT_CAPTURE_INTERVAL;
        image_quality = DEFAULT_IMAGE_QUALITY;

        return;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        // 删除空白字符
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.length() > 0)
            line.erase(line.find_last_not_of(" \t") + 1);

        // 跳过空行和注释
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // 解析段落
        if (line[0] == '[' && line[line.size() - 1] == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        // 解析键值对
        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);

            // 删除键和值的空白字符
            key.erase(0, key.find_first_not_of(" \t"));
            if (key.length() > 0)
                key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            if (value.length() > 0)
                value.erase(value.find_last_not_of(" \t") + 1);

            if (current_section == "Server") {
                if (key == "url") server_url = value;
                else if (key == "verify_ssl") verify_ssl = (value == "true" || value == "1");
            }
            else if (current_section == "Capture") {
                if (key == "interval") try { capture_interval = std::stod(value); }
                catch (...) {}
                else if (key == "quality") try { image_quality = std::stoi(value); }
                catch (...) {}
            }
            else if (current_section == "Game") {
                if (key == "window_title") window_title = value;
            }
            else if (current_section == "Connection") {
                if (key == "max_retries") try { max_retries = std::stoi(value); }
                catch (...) {}
                else if (key == "retry_delay") try { retry_delay = std::stoi(value); }
                catch (...) {}
                else if (key == "heartbeat_interval") try { heartbeat_interval = std::stoi(value); }
                catch (...) {}
            }
        }
    }

    logInfo_fmt("已加载配置文件: {}", filename);
}

bool DNFAutoClient::initialize() {
    // 初始化日志系统
    try {
        Logger::getInstance().setLogFile("logs/client.log");
        Logger::getInstance().setLevel(LogLevel::INFO);
        logInfo("DNF自动化客户端初始化中...");
    }
    catch (const std::exception& e) {
        std::cerr << "日志初始化失败: " << e.what() << std::endl;
        return false;
    }

    // 初始化屏幕捕获
    if (!screen_capture_.initialize(config_.window_title)) {
        logError("初始化屏幕捕获失败");
        return false;
    }

    // 设置屏幕捕获的最小间隔
    screen_capture_.setMinimumCaptureInterval(static_cast<int>(config_.capture_interval * 500));

    // 初始化输入模拟器
    if (!input_simulator_.initialize()) {
        logError("初始化输入模拟器失败");
        return false;
    }

    // 初始化状态机
    initializeStateMachine();

    // 设置WebSocket消息回调
    ws_client_.setMessageCallback([this](const std::string& message) {
        this->processServerResponse(message);
    });

    logInfo("初始化完成");
    return true;
}

void DNFAutoClient::initializeStateMachine() {
    // 定义状态转换函数
    state_handlers_[ClientState::DISCONNECTED] = [this]() { handleDisconnectedState(); };
    state_handlers_[ClientState::CONNECTING] = [this]() { handleConnectingState(); };
    state_handlers_[ClientState::CONNECTED] = [this]() { handleConnectedState(); };
    state_handlers_[ClientState::ACTIVE] = [this]() { handleActiveState(); };
    state_handlers_[ClientState::PAUSED] = [this]() { handlePausedState(); };
    state_handlers_[ClientState::ERROR] = [this]() { handleErrorState(); };
}

void DNFAutoClient::run() {
    if (running_) {
        logWarn("客户端已在运行中");
        return;
    }

    running_ = true;

    // 更新状态
    changeState(ClientState::DISCONNECTED);

    // 启动主线程
    main_thread_ = std::thread(&DNFAutoClient::mainLoop, this);

    logInfo("客户端开始运行，监控中...");
}

void DNFAutoClient::stop() {
    if (!running_) {
        return;
    }

    // 停止标志
    running_ = false;

    // 通知动作线程退出
    action_cv_.notify_all();

    // 通知状态监控线程退出
    status_cv_.notify_all();

    // 等待线程结束
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    if (action_thread_.joinable()) {
        action_thread_.join();
    }
    if (status_thread_.joinable()) {
        status_thread_.join();
    }

    // 断开WebSocket连接
    ws_client_.disconnect();

    // 释放所有可能按下的按键
    input_simulator_.releaseAllKeys();

    logInfo("客户端已停止");
}

void DNFAutoClient::pause() {
    if (current_state_ == ClientState::ACTIVE) {
        changeState(ClientState::PAUSED);
        logInfo("客户端已暂停");
    }
}

void DNFAutoClient::resume() {
    if (current_state_ == ClientState::PAUSED) {
        changeState(ClientState::ACTIVE);
        logInfo("客户端已恢复");
    }
}

void DNFAutoClient::mainLoop() {
    logInfo("主线程启动");

    // 启动动作线程
    action_thread_ = std::thread(&DNFAutoClient::actionThread, this);

    // 启动状态监控线程
    status_thread_ = std::thread(&DNFAutoClient::statusMonitorThread, this);

    // 主循环 - 状态机
    while (running_) {
        try {
            // 执行当前状态的处理函数
            auto it = state_handlers_.find(current_state_);
            if (it != state_handlers_.end()) {
                it->second();
            } else {
                logError_fmt("未处理的状态: {}", static_cast<int>(current_state_));
                changeState(ClientState::ERROR);
            }

            // 防止CPU过度使用
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        catch (const std::exception& e) {
            logError_fmt("主循环异常: {}", e.what());
            consecutive_errors_++;

            // 如果连续错误太多，切换到错误状态
            if (consecutive_errors_ > 5) {
                changeState(ClientState::ERROR);
            }

            // 短暂暂停后继续
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    logInfo("主线程已结束");
}

void DNFAutoClient::changeState(ClientState new_state) {
    // 如果状态未改变，不做任何事
    if (current_state_ == new_state) {
        return;
    }

    // 记录状态转换
    logInfo_fmt("状态转换: {} -> {}",
              STATE_NAMES.at(current_state_),
              STATE_NAMES.at(new_state));

    // 执行状态退出操作
    switch (current_state_) {
        case ClientState::ACTIVE:
            // 退出活动状态时清空动作队列
            clearActionQueue();
            break;
        default:
            break;
    }

    // 更新状态
    current_state_ = new_state;

    // 执行状态进入操作
    switch (current_state_) {
        case ClientState::CONNECTED:
            // 连接成功时重置错误计数
            consecutive_errors_ = 0;
            break;
        case ClientState::ACTIVE:
            // 进入活动状态时重置捕获时间
            last_capture_time_ = 0;
            break;
        case ClientState::ERROR:
            // 进入错误状态时记录错误
            logError("客户端进入错误状态");
            break;
        default:
            break;
    }

    // 通知状态监控线程
    status_cv_.notify_all();
}

void DNFAutoClient::handleDisconnectedState() {
    // 尝试连接到服务器
    changeState(ClientState::CONNECTING);
}

void DNFAutoClient::handleConnectingState() {
    logInfo_fmt("尝试连接到服务器: {}", config_.server_url);

    if (ws_client_.connect(config_.server_url, config_.verify_ssl)) {
        // 连接成功
        changeState(ClientState::CONNECTED);
        retry_count_ = 0;
    } else {
        // 连接失败
        retry_count_++;
        logError_fmt("连接失败，重试 {}/{}", retry_count_, config_.max_retries);

        if (retry_count_ >= config_.max_retries) {
            logError("达到最大重试次数，进入错误状态");
            changeState(ClientState::ERROR);
        } else {
            // 计算重试延迟（指数退避）
            reconnect_delay_ = config_.retry_delay * (1 << std::min(retry_count_, 5));

            // 添加随机抖动
            std::uniform_int_distribution<int> dist(1, 5);
            reconnect_delay_ += dist(random_engine_);

            logInfo_fmt("将在 {} 秒后重试", reconnect_delay_);

            // 等待重试延迟
            for (int i = 0; i < reconnect_delay_ && running_; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // 如果还在运行，回到断开连接状态重新开始
            if (running_) {
                changeState(ClientState::DISCONNECTED);
            }
        }
    }
}

void DNFAutoClient::handleConnectedState() {
    // 连接成功后，进入活动状态
    changeState(ClientState::ACTIVE);
}

void DNFAutoClient::handleActiveState() {
    // 检查是否仍然连接
    if (!ws_client_.isConnected()) {
        logError("与服务器的连接已断开");
        changeState(ClientState::DISCONNECTED);
        return;
    }

    // 检查游戏窗口是否有效
    if (!screen_capture_.isWindowValid()) {
        logWarn("游戏窗口无效，尝试重新初始化");
        if (!screen_capture_.initialize(config_.window_title)) {
            logError("重新初始化屏幕捕获失败");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return;
        }
    }

    // 检查是否是时候发送心跳
    auto now = std::chrono::steady_clock::now();
    auto ms_since_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(last_heartbeat_time_))).count();

    if (ms_since_heartbeat >= config_.heartbeat_interval * 1000) {
        // 发送心跳
        updateGameState();
        ws_client_.sendHeartbeat(game_state_);
        last_heartbeat_time_ = now.time_since_epoch().count();
    }

    // 检查是否是时候捕获屏幕
    auto ms_since_capture = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(last_capture_time_))).count();

    if (ms_since_capture >= config_.capture_interval * 1000) {
        // 捕获屏幕
        auto capture_result = screen_capture_.captureScreen(config_.image_quality);
        if (!capture_result) {
            logError("屏幕捕获失败");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return;
        }

        // 检查图像是否有明显变化
        bool significant_change = capture_result->changed;

        // 如果图像有显著变化或上次发送已经过去较长时间，则发送图像
        if (significant_change || ms_since_capture >= config_.capture_interval * 3000) {
            // 更新游戏状态
            updateGameState();

            // 发送图像到服务器
            if (!ws_client_.sendImage(capture_result->jpeg_data, game_state_, capture_result->window_rect)) {
                logError("发送图像失败");
                consecutive_errors_++;

                if (consecutive_errors_ > 3) {
                    logError("连续发送失败，断开连接");
                    changeState(ClientState::DISCONNECTED);
                    return;
                }
            } else {
                // 发送成功，重置错误计数
                consecutive_errors_ = 0;
            }

            // 更新最后捕获时间
            last_capture_time_ = now.time_since_epoch().count();
        }
    }
}

void DNFAutoClient::handlePausedState() {
    // 暂停状态下，只发送心跳，不发送图像
    auto now = std::chrono::steady_clock::now();
    auto ms_since_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(last_heartbeat_time_))).count();

    if (ms_since_heartbeat >= config_.heartbeat_interval * 1000) {
        // 发送心跳
        updateGameState();
        ws_client_.sendHeartbeat(game_state_);
        last_heartbeat_time_ = now.time_since_epoch().count();
    }

    // 暂停状态下睡眠更长时间
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void DNFAutoClient::handleErrorState() {
    // 错误状态下，等待一段时间后尝试重新连接
    logInfo("处于错误状态，将在5秒后尝试恢复");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 释放所有可能按下的按键
    input_simulator_.releaseAllKeys();

    // 断开连接以便重新连接
    ws_client_.disconnect();

    // 重置重试计数
    retry_count_ = 0;

    // 回到断开连接状态
    changeState(ClientState::DISCONNECTED);
}

void DNFAutoClient::statusMonitorThread() {
    logInfo("状态监控线程启动");

    while (running_) {
        try {
            // 等待状态变化或超时
            std::unique_lock<std::mutex> lock(status_mutex_);
            status_cv_.wait_for(lock, std::chrono::seconds(10));

            if (!running_) break;

            // 监控状态和性能指标
            std::ostringstream status;
            status << "状态: " << STATE_NAMES.at(current_state_)
                   << ", 队列动作: " << action_queue_.size()
                   << ", 已执行动作: " << action_counter_;

            // 对于每种状态可能有特定的监控
            switch (current_state_) {
                case ClientState::ACTIVE: {
                    auto now = std::chrono::steady_clock::now();
                    auto seconds_since_last_action = std::chrono::duration_cast<std::chrono::seconds>(
                        now - std::chrono::steady_clock::time_point(
                            std::chrono::steady_clock::duration(last_action_time_))).count();

                    status << ", 上次动作: " << seconds_since_last_action << "秒前";

                    // 检查是否长时间没有动作
                    if (seconds_since_last_action > 60 && action_counter_ > 0) {
                        logWarn("60秒内没有执行动作，可能需要检查连接");
                    }
                    break;
                }
                case ClientState::ERROR:
                    status << ", 错误计数: " << consecutive_errors_;
                    break;
                default:
                    break;
            }

            // 每隔30秒记录一次状态
            static auto last_log_time = std::chrono::steady_clock::now();
            auto seconds_since_last_log = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_log_time).count();

            if (seconds_since_last_log >= 30) {
                logInfo(status.str());
                last_log_time = now;
            }
        }
        catch (const std::exception& e) {
            logError_fmt("状态监控线程异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    logInfo("状态监控线程已结束");
}

void DNFAutoClient::actionThread() {
    logInfo("动作执行线程启动");

    while (running_) {
        try {
            std::unique_lock<std::mutex> lock(action_mutex_);

            // 等待有动作要执行或需要停止
            action_cv_.wait(lock, [this] {
                return !running_ || !action_queue_.empty();
            });

            // 如果收到停止信号，则退出
            if (!running_) {
                break;
            }

            // 获取队列中的下一个动作
            if (!action_queue_.empty()) {
                Action action = action_queue_.front();
                action_queue_.pop();
                lock.unlock();  // 解锁以便在执行动作时不阻塞其他线程

                // 执行动作
                executeAction(action);

                // 更新统计信息
                action_counter_++;
                last_action_time_ = std::chrono::steady_clock::now().time_since_epoch().count();
            }
        }
        catch (const std::exception& e) {
            logError_fmt("动作线程异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    logInfo("动作执行线程已结束");
}

void DNFAutoClient::clearActionQueue() {
    std::unique_lock<std::mutex> lock(action_mutex_);
    std::queue<Action> empty;
    std::swap(action_queue_, empty);
    logInfo("已清空动作队列");
}

void DNFAutoClient::processServerResponse(const std::string& response) {
    try {
        // 使用nlohmann-json解析
        json data = json::parse(response);

        // 获取消息类型
        std::string message_type = data["type"];

        if (message_type == "action_response") {
            handleActionResponse(data);
        }
        else if (message_type == "heartbeat_response") {
            handleHeartbeatResponse(data);
        }
        else if (message_type == "error") {
            handleErrorResponse(data);
        }
        else {
            logWarn_fmt("收到未知类型的消息: {}", message_type);
        }
    }
    catch (const json::parse_error& e) {
        logError_fmt("JSON解析错误: {}", e.what());
    }
    catch (const std::exception& e) {
        logError_fmt("处理服务器响应时异常: {}", e.what());
    }
}

void DNFAutoClient::handleActionResponse(const json& data) {
    logInfo("收到服务器动作响应");

    try {
        // 检查是否含有动作数组
        if (!data.contains("actions") || !data["actions"].is_array()) {
            logWarn("动作响应中没有有效的动作数组");
            return;
        }

        // 获取动作数组
        auto actions = data["actions"];

        // 如果当前在暂停状态，不添加动作
        if (current_state_ == ClientState::PAUSED) {
            logInfo_fmt("客户端已暂停，忽略 {} 个动作", actions.size());
            return;
        }

        std::unique_lock<std::mutex> lock(action_mutex_);

        // 检查队列大小限制
        size_t max_queue_size = 20;  // 最大队列大小
        if (action_queue_.size() >= max_queue_size) {
            logWarn_fmt("动作队列已满 ({}), 清空旧动作", max_queue_size);
            std::queue<Action> empty;
            std::swap(action_queue_, empty);
        }

        // 添加动作到队列
        int added_count = 0;
        for (const auto& action_json : actions) {
            Action action;

            // 必填字段
            action.type = action_json.value("type", "unknown");

            // 可选字段
            if (action_json.contains("key")) action.key = action_json["key"];
            if (action_json.contains("keys") && action_json["keys"].is_array()) {
                for (const auto& key : action_json["keys"]) {
                    action.keys.push_back(key);
                }
            }
            if (action_json.contains("position") && action_json["position"].is_array()) {
                for (const auto& pos : action_json["position"]) {
                    action.position.push_back(pos);
                }
            }
            if (action_json.contains("delay")) action.delay = action_json["delay"];
            if (action_json.contains("purpose")) action.purpose = action_json["purpose"];
            if (action_json.contains("description")) action.description = action_json["description"];

            // 添加到队列
            action_queue_.push(action);
            added_count++;
        }

        lock.unlock();

        // 通知动作线程
        action_cv_.notify_one();

        logInfo_fmt("已添加 {} 个动作到队列", added_count);
    }
    catch (const std::exception& e) {
        logError_fmt("处理动作响应时异常: {}", e.what());
    }
}

void DNFAutoClient::handleHeartbeatResponse(const json& data) {
    // 简单记录心跳响应
    logDebug("收到心跳响应");

    // 检查服务器状态信息
    if (data.contains("server_stats")) {
        auto stats = data["server_stats"];
        logDebug_fmt("服务器状态: 连接数={}, 已处理图像={}, 已生成动作={}",
                   stats.value("connections", 0),
                   stats.value("images_processed", 0),
                   stats.value("actions_generated", 0));
    }
}

void DNFAutoClient::handleErrorResponse(const json& data) {
    // 处理错误响应
    std::string error_code = data.value("error", "unknown_error");
    std::string error_message = data.value("message", "未知错误");

    logError_fmt("服务器返回错误: {}，消息: {}", error_code, error_message);

    // 增加错误计数
    consecutive_errors_++;

    // 如果连续错误太多，切换到错误状态
    if (consecutive_errors_ > 5) {
        changeState(ClientState::ERROR);
    }
}

void DNFAutoClient::executeAction(const Action& action) {
    try {
        // 执行动作前等待指定的延迟
        if (action.delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(action.delay * 1000)));
        }

        logInfo_fmt("执行动作: {}", action.description.empty() ? action.type : action.description);

        if (action.type == "move_to" && action.position.size() >= 2) {
            // 移动到指定位置
            input_simulator_.simulateMouseMove(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]),
                true);  // 使用平滑移动
        }
        else if (action.type == "click" && action.position.size() >= 2) {
            // 移动并点击
            input_simulator_.simulateMouseMove(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]),
                true);  // 使用平滑移动
            input_simulator_.simulateMouseClick(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]));
        }
        else if (action.type == "double_click" && action.position.size() >= 2) {
            // 移动并双击
            input_simulator_.simulateMouseMove(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]),
                true);  // 使用平滑移动
            input_simulator_.simulateDoubleClick(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]));
        }
        else if (action.type == "drag" && action.position.size() >= 4) {
            // 拖拽
            input_simulator_.simulateMouseDrag(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]),
                static_cast<int>(action.position[2]),
                static_cast<int>(action.position[3]));
        }
        else if (action.type == "use_skill" && !action.key.empty()) {
            // 使用技能
            input_simulator_.simulateKeyPress(action.key);
        }
        else if (action.type == "interact" && !action.key.empty()) {
            // 交互
            input_simulator_.simulateKeyPress(action.key);
        }
        else if (action.type == "press_key_combo" && !action.keys.empty()) {
            // 按键组合
            input_simulator_.simulateKeyCombo(action.keys);
        }
        else if (action.type == "hold_key" && !action.key.empty() && action.position.size() >= 1) {
            // 长按按键
            int duration = static_cast<int>(action.position[0]);
            input_simulator_.simulateKeyHold(action.key, duration);
        }
        else if (action.type == "common_action" && !action.key.empty()) {
            // 常用动作
            input_simulator_.simulateCommonAction(action.key);
        }
        else if (action.type == "move_random") {
            // 随机移动
            // 简单实现，随机选择屏幕上的点
            RECT window_rect = screen_capture_.getWindowRect();
            int x = window_rect.left + input_simulator_.addHumanJitter(
                (window_rect.right - window_rect.left) / 2, 100);
            int y = window_rect.top + input_simulator_.addHumanJitter(
                (window_rect.bottom - window_rect.top) / 2, 100);

            input_simulator_.simulateMouseMove(x, y, true);
        }
        else if (action.type == "stop") {
            // 停止所有动作
            input_simulator_.releaseAllKeys();

            // 清空动作队列
            clearActionQueue();
        }
        else {
            logWarn_fmt("未知的动作类型: {}", action.type);
        }
    }
    catch (const std::exception& e) {
        logError_fmt("执行动作时异常: {}", e.what());
    }
}

void DNFAutoClient::updateGameState() {
    // 这里可以实现游戏状态的更新逻辑
    // 例如，通过图像识别或内存读取等方式获取游戏状态

    // 获取窗口位置
    RECT window_rect = screen_capture_.getWindowRect();

    // 更新玩家位置（模拟中心位置）
    game_state_.player_x = window_rect.left + (window_rect.right - window_rect.left) / 2;
    game_state_.player_y = window_rect.top + (window_rect.bottom - window_rect.top) / 2;

    // 模拟其他状态（实际应用中应该通过识别获取）
    game_state_.hp_percent = 100.0f;
    game_state_.mp_percent = 100.0f;
    game_state_.inventory_full = false;

    // 更新技能冷却时间（模拟）
    auto now = std::chrono::steady_clock::now();
    auto seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // 模拟每30秒清理过期的冷却
    if (seconds_since_epoch % 30 == 0) {
        for (auto it = game_state_.cooldowns.begin(); it != game_state_.cooldowns.end();) {
            if (it->second <= 0) {
                it = game_state_.cooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 模拟冷却时间减少
    for (auto& cooldown : game_state_.cooldowns) {
        cooldown.second -= 0.1f;  // 减少0.1秒
        if (cooldown.second < 0) {
            cooldown.second = 0;
        }
    }
}

std::string DNFAutoClient::getClientInfo() {
    // 获取客户端基本信息
    std::ostringstream os;
    os << "{"
       << "\"version\":\"1.0.0\","
       << "\"platform\":\"windows\","
       << "\"screen_width\":" << GetSystemMetrics(SM_CXSCREEN) << ","
       << "\"screen_height\":" << GetSystemMetrics(SM_CYSCREEN) << ","
       << "\"state\":\"" << STATE_NAMES.at(current_state_) << "\","
       << "\"actions_executed\":" << action_counter_
       << "}";
    return os.str();
}

ClientState DNFAutoClient::getState() const {
    return current_state_;
}

std::string DNFAutoClient::getStateString() const {
    auto it = STATE_NAMES.find(current_state_);
    if (it != STATE_NAMES.end()) {
        return it->second;
    }
    return "未知";
}