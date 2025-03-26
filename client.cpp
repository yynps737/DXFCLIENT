#include "client.h"
#include "LogWrapper.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

DNFAutoClient::DNFAutoClient() : running_(false) {
    // 加载配置
    config_.load_from_file("config.ini");
}

DNFAutoClient::~DNFAutoClient() {
    stop();
}

void DNFAutoClient::ClientConfig::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        logWarn_fmt("无法打开配置文件: {}，使用默认值", filename);
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

    // 初始化输入模拟器
    if (!input_simulator_.initialize()) {
        logError("初始化输入模拟器失败");
        return false;
    }

    logInfo("初始化完成");
    return true;
}

void DNFAutoClient::run() {
    if (running_) {
        logWarn("客户端已在运行中");
        return;
    }

    running_ = true;

    // 连接到服务器
    if (!ws_client_.connect(config_.server_url, config_.verify_ssl)) {
        logError_fmt("无法连接到服务器: {}", config_.server_url);
        running_ = false;
        return;
    }

    // 设置WebSocket消息回调
    ws_client_.setMessageCallback([this](const std::string& message) {
        this->processServerResponse(message);
    });

    // 启动线程
    capture_thread_ = std::thread(&DNFAutoClient::captureThread, this);
    action_thread_ = std::thread(&DNFAutoClient::actionThread, this);

    logInfo("客户端开始运行，监控中...");
}

void DNFAutoClient::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 通知动作线程退出
    action_cv_.notify_all();

    // 等待线程结束
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (action_thread_.joinable()) {
        action_thread_.join();
    }

    // 断开WebSocket连接
    ws_client_.disconnect();

    logInfo("客户端已停止");
}

void DNFAutoClient::captureThread() {
    logInfo("捕获线程启动中");

    auto next_capture_time = std::chrono::steady_clock::now();
    auto next_heartbeat_time = std::chrono::steady_clock::now();

    while (running_) {
        auto now = std::chrono::steady_clock::now();

        // 发送心跳
        if (now >= next_heartbeat_time) {
            updateGameState();
            ws_client_.sendHeartbeat(game_state_);
            next_heartbeat_time = now + std::chrono::seconds(config_.heartbeat_interval);
        }

        // 捕获屏幕并发送
        if (now >= next_capture_time) {
            // 检查游戏窗口是否有效
            if (!screen_capture_.isWindowValid()) {
                if (!screen_capture_.initialize(config_.window_title)) {
                    logWarn("找不到游戏窗口，重试中...");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }

            // 捕获屏幕
            auto capture_result = screen_capture_.captureScreen(config_.image_quality);
            if (!capture_result) {
                logError("屏幕捕获失败");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            // 更新游戏状态
            updateGameState();

            // 发送图像到服务器
            if (!ws_client_.sendImage(capture_result->jpeg_data, game_state_, capture_result->window_rect)) {
                logError("发送图像失败");
            }

            // 计算下一次捕获时间
            next_capture_time = now + std::chrono::milliseconds(
                static_cast<int>(config_.capture_interval * 1000));
        }

        // 降低CPU资源使用
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    logInfo("捕获线程已结束");
}

void DNFAutoClient::actionThread() {
    logInfo("动作执行线程启动中");

    while (running_) {
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
        }
    }

    logInfo("动作执行线程已结束");
}

void DNFAutoClient::processServerResponse(const std::string& response) {
    try {
        // 基本JSON解析 (没有使用rapidjson)
        std::map<std::string, std::string> jsonValues;
        std::string token = "\"type\":\"";
        size_t pos = response.find(token);
        if (pos != std::string::npos) {
            pos += token.length();
            size_t endPos = response.find("\"", pos);
            if (endPos != std::string::npos) {
                std::string type = response.substr(pos, endPos - pos);

                if (type == "action_response") {
                    logInfo("收到服务器动作响应");
                    // 这里我们简化处理，在真实情况下需要正确解析JSON
                    // 假设收到了一个随机移动的动作

                    std::unique_lock<std::mutex> lock(action_mutex_);

                    // 清空队列
                    std::queue<Action> empty;
                    std::swap(action_queue_, empty);

                    // 添加一个简单动作
                    Action action;
                    action.type = "move_random";
                    action.description = "随机移动(简化命令)";
                    action_queue_.push(action);

                    lock.unlock();
                    action_cv_.notify_one();

                    logInfo_fmt("收到 {} 个动作", action_queue_.size());
                }
                else if (type == "heartbeat_response") {
                    // 处理心跳响应
                    logDebug("收到心跳响应");
                }
                else if (type == "error") {
                    // 处理错误
                    logError("服务器返回错误");
                }
            }
        }
    }
    catch (const std::exception& e) {
        logError_fmt("处理服务器响应时异常: {}", e.what());
    }
}

void DNFAutoClient::executeAction(const Action& action) {
    try {
        // 执行动作前等待指定的延迟
        if (action.delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(action.delay * 1000)));
        }

        logInfo_fmt("执行动作: {}", action.description);

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
                static_cast<int>(action.position[1]));
            input_simulator_.simulateMouseClick(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]));
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
            // 这里可以实现一些停止逻辑，比如松开所有按键等
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

    // 示例：设置玩家位置（实际应该用真实数据）
    RECT window_rect = screen_capture_.getWindowRect();
    game_state_.player_x = window_rect.left + (window_rect.right - window_rect.left) / 2;
    game_state_.player_y = window_rect.top + (window_rect.bottom - window_rect.top) / 2;
}

std::string DNFAutoClient::getClientInfo() {
    std::ostringstream os;
    os << "{"
       << "\"version\":\"1.0.0\","
       << "\"platform\":\"windows\","
       << "\"screen_width\":" << GetSystemMetrics(SM_CXSCREEN) << ","
       << "\"screen_height\":" << GetSystemMetrics(SM_CYSCREEN)
       << "}";
    return os.str();
}