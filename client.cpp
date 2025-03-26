#define NOMINMAX

#include "client.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>

DNFAutoClient::DNFAutoClient() : running_(false) {
    // ��������
    config_.load_from_file("config.ini");
}

DNFAutoClient::~DNFAutoClient() {
    stop();
}

void DNFAutoClient::ClientConfig::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        spdlog::warn("�޷��������ļ�: {}��ʹ��Ĭ��ֵ", filename);
        return;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        // ɾ���հ��ַ�
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.length() > 0)
            line.erase(line.find_last_not_of(" \t") + 1);

        // �������к�ע��
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // ����ڱ���
        if (line[0] == '[' && line[line.size() - 1] == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        // �����ֵ��
        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);

            // ɾ������ֵ�Ŀհ��ַ�
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

    spdlog::info("�Ѽ��������ļ�: {}", filename);
}

bool DNFAutoClient::initialize() {
    // ��ʼ����־ϵͳ
    try {
        auto logger = spdlog::rotating_logger_mt("dnf_client", "logs/client.log",
            1024 * 1024 * 5, 3);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }
    catch (const std::exception& e) {
        std::cerr << "��־��ʼ��ʧ��: " << e.what() << std::endl;
        return false;
    }

    spdlog::info("DNF�Զ����ͻ��˳�ʼ����...");

    // ��ʼ����Ļ����
    if (!screen_capture_.initialize(config_.window_title)) {
        spdlog::error("��ʼ����Ļ����ʧ��");
        return false;
    }

    // ��ʼ������ģ����
    if (!input_simulator_.initialize()) {
        spdlog::error("��ʼ������ģ����ʧ��");
        return false;
    }

    spdlog::info("��ʼ�����");
    return true;
}

void DNFAutoClient::run() {
    if (running_) {
        spdlog::warn("�ͻ�������������");
        return;
    }

    running_ = true;

    // ���ӵ�������
    if (!ws_client_.connect(config_.server_url, config_.verify_ssl)) {
        spdlog::error("�޷����ӵ�������: {}", config_.server_url);
        running_ = false;
        return;
    }

    // ����WebSocket��Ϣ�ص�
    ws_client_.setMessageCallback([this](const std::string& message) {
        this->processServerResponse(message);
        });

    // �����߳�
    capture_thread_ = std::thread(&DNFAutoClient::captureThread, this);
    action_thread_ = std::thread(&DNFAutoClient::actionThread, this);

    spdlog::info("�ͻ�������������������...");
}

void DNFAutoClient::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // ֪ͨ�����߳��˳�
    action_cv_.notify_all();

    // �ȴ��߳̽���
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (action_thread_.joinable()) {
        action_thread_.join();
    }

    // �Ͽ�WebSocket����
    ws_client_.disconnect();

    spdlog::info("�ͻ�����ֹͣ");
}

void DNFAutoClient::captureThread() {
    spdlog::info("�����߳�������");

    auto next_capture_time = std::chrono::steady_clock::now();
    auto next_heartbeat_time = std::chrono::steady_clock::now();

    while (running_) {
        auto now = std::chrono::steady_clock::now();

        // ����������
        if (now >= next_heartbeat_time) {
            updateGameState();
            ws_client_.sendHeartbeat(game_state_);
            next_heartbeat_time = now + std::chrono::seconds(config_.heartbeat_interval);
        }

        // ������Ļ������
        if (now >= next_capture_time) {
            // �����Ϸ�����Ƿ���Ч
            if (!screen_capture_.isWindowValid()) {
                if (!screen_capture_.initialize(config_.window_title)) {
                    spdlog::warn("�Ҳ�����Ϸ���ڣ�������...");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }

            // ������Ļ
            auto capture_result = screen_capture_.captureScreen(config_.image_quality);
            if (!capture_result) {
                spdlog::error("��Ļ����ʧ��");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            // ������Ϸ״̬
            updateGameState();

            // ����ͼ�񵽷�����
            if (!ws_client_.sendImage(capture_result->jpeg_data, game_state_, capture_result->window_rect)) {
                spdlog::error("����ͼ��ʧ��");
            }

            // ������һ�β���ʱ��
            next_capture_time = now + std::chrono::milliseconds(
                static_cast<int>(config_.capture_interval * 1000));
        }

        // ����CPU����ʹ��
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    spdlog::info("�����߳��ѽ���");
}

void DNFAutoClient::actionThread() {
    spdlog::info("����ִ���߳�������");

    while (running_) {
        std::unique_lock<std::mutex> lock(action_mutex_);

        // �ȴ��ж�����Ҫִ�л�����Ҫֹͣ
        action_cv_.wait(lock, [this] {
            return !running_ || !action_queue_.empty();
            });

        // ����յ�ֹͣ�źţ����˳�
        if (!running_) {
            break;
        }

        // ��ȡ�����е���һ������
        if (!action_queue_.empty()) {
            Action action = action_queue_.front();
            action_queue_.pop();
            lock.unlock();  // �������Ա���ִ�ж���ʱ�����������߳�

            // ִ�ж���
            executeAction(action);
        }
    }

    spdlog::info("����ִ���߳��ѽ���");
}

void DNFAutoClient::processServerResponse(const std::string& response) {
    try {
        rapidjson::Document doc;
        doc.Parse(response.c_str());

        if (doc.HasParseError()) {
            spdlog::error("������������Ӧʱ����");
            return;
        }

        // ����ͬ���͵���Ϣ
        if (doc.HasMember("type")) {
            std::string type = doc["type"].GetString();

            if (type == "action_response") {
                // ������������صĶ���
                if (doc.HasMember("actions") && doc["actions"].IsArray()) {
                    const auto& actions = doc["actions"];

                    std::unique_lock<std::mutex> lock(action_mutex_);

                    // ��յ�ǰ���У�����µĶ���
                    std::queue<Action> empty;
                    std::swap(action_queue_, empty);

                    for (rapidjson::SizeType i = 0; i < actions.Size(); i++) {
                        const auto& action_json = actions[i];
                        Action action;

                        // ��JSON����ȡ������Ϣ
                        if (action_json.HasMember("type")) {
                            action.type = action_json["type"].GetString();
                        }

                        if (action_json.HasMember("key")) {
                            action.key = action_json["key"].GetString();
                        }

                        if (action_json.HasMember("keys") && action_json["keys"].IsArray()) {
                            const auto& keys = action_json["keys"];
                            for (rapidjson::SizeType j = 0; j < keys.Size(); j++) {
                                action.keys.push_back(keys[j].GetString());
                            }
                        }

                        if (action_json.HasMember("position") && action_json["position"].IsArray()) {
                            const auto& pos = action_json["position"];
                            for (rapidjson::SizeType j = 0; j < pos.Size(); j++) {
                                action.position.push_back(pos[j].GetFloat());
                            }
                        }

                        if (action_json.HasMember("delay")) {
                            action.delay = action_json["delay"].GetFloat();
                        }

                        if (action_json.HasMember("purpose")) {
                            action.purpose = action_json["purpose"].GetString();
                        }

                        if (action_json.HasMember("description")) {
                            action.description = action_json["description"].GetString();
                        }

                        // ��ӵ�����
                        action_queue_.push(action);
                    }

                    lock.unlock();

                    // ֪ͨ�����߳�
                    action_cv_.notify_one();

                    spdlog::info("�յ� {} ������", action_queue_.size());
                }
            }
            else if (type == "heartbeat_response") {
                // ����������Ӧ
                spdlog::debug("�յ�������Ӧ");
            }
            else if (type == "error") {
                // �������
                std::string error_message = "δ֪����";
                if (doc.HasMember("message")) {
                    error_message = doc["message"].GetString();
                }
                spdlog::error("����������: {}", error_message);
            }
        }
    }
    catch (const std::exception& e) {
        spdlog::error("�����������Ӧʱ�쳣: {}", e.what());
    }
}

void DNFAutoClient::executeAction(const Action& action) {
    try {
        // ִ�ж���ǰ�ȴ�ָ�����ӳ�
        if (action.delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(action.delay * 1000)));
        }

        spdlog::info("ִ�ж���: {}", action.description);

        if (action.type == "move_to" && action.position.size() >= 2) {
            // �ƶ���ָ��λ��
            input_simulator_.simulateMouseMove(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]),
                true);  // ʹ��ƽ���ƶ�
        }
        else if (action.type == "click" && action.position.size() >= 2) {
            // �ƶ������
            input_simulator_.simulateMouseMove(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]));
            input_simulator_.simulateMouseClick(
                static_cast<int>(action.position[0]),
                static_cast<int>(action.position[1]));
        }
        else if (action.type == "use_skill" && !action.key.empty()) {
            // ʹ�ü���
            input_simulator_.simulateKeyPress(action.key);
        }
        else if (action.type == "interact" && !action.key.empty()) {
            // ����
            input_simulator_.simulateKeyPress(action.key);
        }
        else if (action.type == "press_key_combo" && !action.keys.empty()) {
            // �������
            input_simulator_.simulateKeyCombo(action.keys);
        }
        else if (action.type == "move_random") {
            // ����ƶ�
            // ��ʵ�֣����ѡ����Ļ�ϵĵ�
            RECT window_rect = screen_capture_.getWindowRect();
            int x = window_rect.left + input_simulator_.addHumanJitter(
                (window_rect.right - window_rect.left) / 2, 100);
            int y = window_rect.top + input_simulator_.addHumanJitter(
                (window_rect.bottom - window_rect.top) / 2, 100);

            input_simulator_.simulateMouseMove(x, y, true);
        }
        else if (action.type == "stop") {
            // ֹͣ���ж���
            // �������ʵ��һЩֹͣ�߼������ɿ����а�����
        }
        else {
            spdlog::warn("δ֪��������: {}", action.type);
        }
    }
    catch (const std::exception& e) {
        spdlog::error("ִ�ж���ʱ�쳣: {}", e.what());
    }
}

void DNFAutoClient::updateGameState() {
    // �������ʵ����Ϸ״̬�ĸ����߼�
    // ���磬ͨ��ͼ��ʶ���ڴ��ȡ�ȷ�ʽ��ȡ��Ϸ״̬

    // ��ʾ�����������λ�ã�ʵ��Ӧ����Ӧʹ����ʵ���ݣ�
    RECT window_rect = screen_capture_.getWindowRect();
    game_state_.player_x = window_rect.left + (window_rect.right - window_rect.left) / 2;
    game_state_.player_y = window_rect.top + (window_rect.bottom - window_rect.top) / 2;
}

std::string DNFAutoClient::getClientInfo() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("version");
    writer.String("1.0.0");
    writer.Key("platform");
    writer.String("windows");
    writer.Key("screen_width");
    writer.Int(GetSystemMetrics(SM_CXSCREEN));
    writer.Key("screen_height");
    writer.Int(GetSystemMetrics(SM_CYSCREEN));
    writer.EndObject();

    return buffer.GetString();
}

// ������ڵ�
