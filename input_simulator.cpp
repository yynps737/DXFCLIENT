#include "input_simulator.h"
#include "LogWrapper.h"
#include <map>
#include <random>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <Windows.h>

// 键名到虚拟键代码的映射
static std::map<std::string, UINT> key_map = {
    {"a", 'A'}, {"b", 'B'}, {"c", 'C'}, {"d", 'D'}, {"e", 'E'}, {"f", 'F'}, {"g", 'G'}, {"h", 'H'},
    {"i", 'I'}, {"j", 'J'}, {"k", 'K'}, {"l", 'L'}, {"m", 'M'}, {"n", 'N'}, {"o", 'O'}, {"p", 'P'},
    {"q", 'Q'}, {"r", 'R'}, {"s", 'S'}, {"t", 'T'}, {"u", 'U'}, {"v", 'V'}, {"w", 'W'}, {"x", 'X'},
    {"y", 'Y'}, {"z", 'Z'}, {"0", '0'}, {"1", '1'}, {"2", '2'}, {"3", '3'}, {"4", '4'}, {"5", '5'},
    {"6", '6'}, {"7", '7'}, {"8", '8'}, {"9", '9'}, {"f1", VK_F1}, {"f2", VK_F2}, {"f3", VK_F3},
    {"f4", VK_F4}, {"f5", VK_F5}, {"f6", VK_F6}, {"f7", VK_F7}, {"f8", VK_F8}, {"f9", VK_F9},
    {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12}, {"shift", VK_SHIFT}, {"ctrl", VK_CONTROL},
    {"alt", VK_MENU}, {"tab", VK_TAB}, {"enter", VK_RETURN}, {"space", VK_SPACE}, {"esc", VK_ESCAPE},
    {"escape", VK_ESCAPE}, {"backspace", VK_BACK}, {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT},
    {"right", VK_RIGHT}, {"caps", VK_CAPITAL}, {"capslock", VK_CAPITAL}, {"del", VK_DELETE},
    {"delete", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END}, {"pgup", VK_PRIOR}, {"pgdn", VK_NEXT},
    {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"insert", VK_INSERT}, {"pause", VK_PAUSE},
    {"numlock", VK_NUMLOCK}, {"scroll", VK_SCROLL}, {"win", VK_LWIN}, {"windows", VK_LWIN}
};

// 游戏中常用的按键组合
static std::map<std::string, std::vector<std::string>> common_combos = {
    {"quickslot1", {"1"}},
    {"quickslot2", {"2"}},
    {"quickslot3", {"3"}},
    {"quickslot4", {"4"}},
    {"quickslot5", {"5"}},
    {"quickslot6", {"6"}},
    {"quickslot7", {"7"}},
    {"quickslot8", {"8"}},
    {"quickslot9", {"9"}},
    {"quickslot0", {"0"}},
    {"inventory", {"i"}},
    {"character", {"c"}},
    {"skills", {"k"}},
    {"map", {"m"}},
    {"quest", {"q"}},
    {"chat", {"enter"}},
    {"partychat", {"ctrl", "enter"}},
    {"guildchat", {"alt", "enter"}},
    {"revive", {"alt", "r"}},
    {"screenshot", {"prtsc"}},
    {"esc_menu", {"escape"}}
};

// 随机数生成器
static std::mt19937 rng(std::random_device{}());

InputSimulator::InputSimulator() {
    // 获取当前鼠标位置
    GetCursorPos(&current_mouse_pos_);
    last_click_time_ = 0;
    last_key_time_ = 0;

    // 重置按键状态
    for (auto& key_pair : key_map) {
        key_states_[key_pair.second] = false;
    }

    // 初始化人类模拟参数
    initializeHumanParameters();
}

InputSimulator::~InputSimulator() {
    // 确保所有按键被释放
    releaseAllKeys();
}

bool InputSimulator::initialize() {
    logInfo("输入模拟器初始化");
    initialized_ = true;
    return true;
}

void InputSimulator::releaseAllKeys() {
    // 释放所有可能按下的键
    for (auto& key_pair : key_states_) {
        if (key_pair.second) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key_pair.first;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
            key_pair.second = false;
        }
    }

    // 释放鼠标按钮
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP;
    SendInput(1, &input, sizeof(INPUT));

    logInfo("已释放所有按键和鼠标按钮");
}

void InputSimulator::initializeHumanParameters() {
    // 设置基本人类参数
    human_params_.movement_speed_mean = 15.0;  // 平均移动速度 (像素/帧)
    human_params_.movement_speed_variance = 5.0;  // 速度变化
    human_params_.click_delay_mean = 50.0;  // 平均点击延迟 (毫秒)
    human_params_.click_delay_variance = 20.0;  // 点击延迟变化
    human_params_.double_click_threshold = 500.0;  // 双击阈值 (毫秒)
    human_params_.key_press_delay_mean = 80.0;  // 按键按下延迟 (毫秒)
    human_params_.key_press_delay_variance = 30.0;  // 按键延迟变化
    human_params_.fatigue_factor = 1.0;  // 初始疲劳因子 (1.0 = 正常)
    human_params_.jitter_max = 3.0;  // 最大抖动 (像素)

    // 设置进阶人类参数
    human_params_.acceleration_factor = 0.3;  // 加速因子
    human_params_.deceleration_factor = 0.7;  // 减速因子
    human_params_.curve_randomness = 0.3;  // 曲线随机性

    // 是否在游戏模式 (调整一些参数更适合游戏)
    human_params_.gaming_mode = true;

    // 初始化会话开始时间 (用于模拟疲劳)
    session_start_time_ = std::chrono::steady_clock::now();
}

void InputSimulator::updateHumanParameters() {
    // 计算会话时长 (小时)
    auto now = std::chrono::steady_clock::now();
    double session_hours = std::chrono::duration<double>(now - session_start_time_).count() / 3600.0;

    // 更新疲劳因子 (随着时间增加疲劳)
    if (session_hours < 1.0) {
        // 第一小时几乎没有疲劳
        human_params_.fatigue_factor = 1.0;
    } else if (session_hours < 2.0) {
        // 1-2小时有轻微疲劳
        human_params_.fatigue_factor = 1.0 + (session_hours - 1.0) * 0.1;
    } else if (session_hours < 4.0) {
        // 2-4小时疲劳增加
        human_params_.fatigue_factor = 1.1 + (session_hours - 2.0) * 0.15;
    } else {
        // 4小时以上疲劳显著
        human_params_.fatigue_factor = 1.4 + (session_hours - 4.0) * 0.1;
        // 限制最大疲劳
        human_params_.fatigue_factor = std::min(human_params_.fatigue_factor, 2.0);
    }

    // 根据疲劳调整参数
    double fatigue = human_params_.fatigue_factor;
    human_params_.movement_speed_mean = 15.0 / fatigue;
    human_params_.click_delay_mean = 50.0 * fatigue;
    human_params_.key_press_delay_mean = 80.0 * fatigue;
    human_params_.jitter_max = 3.0 * fatigue;

    // 每隔一段时间增加随机性，模拟注意力波动
    double time_factor = std::sin(session_hours * 3.14159) * 0.1 + 1.0;
    human_params_.movement_speed_variance = 5.0 * time_factor;
    human_params_.click_delay_variance = 20.0 * time_factor;
    human_params_.key_press_delay_variance = 30.0 * time_factor;

    // 记录更新
    if (session_hours > 1.0 && static_cast<int>(session_hours * 10) % 10 == 0) {
        logInfo_fmt("已更新人类参数: 疲劳因子 = {:.2f}, 会话时长 = {:.2f}小时",
                  human_params_.fatigue_factor, session_hours);
    }
}

void InputSimulator::simulateMouseMove(int x, int y, bool smooth) {
    updateHumanParameters();

    if (smooth) {
        // 使用平滑移动
        interpolateMouseMovement(current_mouse_pos_.x, current_mouse_pos_.y, x, y);
    } else {
        // 直接移动
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dx = static_cast<LONG>(x * (65535.0f / GetSystemMetrics(SM_CXSCREEN)));
        input.mi.dy = static_cast<LONG>(y * (65535.0f / GetSystemMetrics(SM_CYSCREEN)));
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

        sendInputEvent(input);
    }

    // 更新当前鼠标位置
    current_mouse_pos_.x = x;
    current_mouse_pos_.y = y;
}

void InputSimulator::simulateMouseClick(int x, int y, bool right_button) {
    // 先移动到指定位置
    simulateMouseMove(x, y);

    // 获取当前时间
    auto now = std::chrono::steady_clock::now();
    auto click_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // 计算距离上次点击的时间
    long time_since_last_click = click_time - last_click_time_;

    // 检查是否是双击 (来自AI决策)
    bool is_double_click = (time_since_last_click < human_params_.double_click_threshold);

    // 如果是双击，使用更短的延迟
    int click_delay = static_cast<int>(
        std::normal_distribution<double>(
            is_double_click ? 20.0 : human_params_.click_delay_mean,
            is_double_click ? 5.0 : human_params_.click_delay_variance
        )(rng)
    );
    click_delay = std::max(10, click_delay);

    // 然后点击
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = right_button ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    sendInputEvent(input);

    // 模拟人类点击的短暂停顿
    std::this_thread::sleep_for(std::chrono::milliseconds(click_delay));

    // 释放鼠标按钮
    input.mi.dwFlags = right_button ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    sendInputEvent(input);

    // 更新上次点击时间
    last_click_time_ = click_time;
}

void InputSimulator::simulateDoubleClick(int x, int y, bool right_button) {
    // 模拟双击
    simulateMouseClick(x, y, right_button);

    // 双击间隔
    int interval = static_cast<int>(std::normal_distribution<double>(120.0, 30.0)(rng));
    interval = std::max(50, std::min(interval, 200));
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));

    // 第二次点击
    simulateMouseClick(x, y, right_button);
}

void InputSimulator::simulateMouseDrag(int start_x, int start_y, int end_x, int end_y) {
    // 先移动到起始位置
    simulateMouseMove(start_x, start_y, true);

    // 按下鼠标按钮
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    sendInputEvent(input);

    // 短暂延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // 平滑移动到结束位置
    interpolateMouseMovement(start_x, start_y, end_x, end_y);

    // 再短暂延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // 释放鼠标按钮
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    sendInputEvent(input);

    // 更新当前鼠标位置
    current_mouse_pos_.x = end_x;
    current_mouse_pos_.y = end_y;
}

void InputSimulator::simulateKeyPress(const std::string& key) {
    updateHumanParameters();

    UINT vk = mapKeyNameToVirtualKey(key);
    if (vk == 0) {
        logError_fmt("未知按键: {}", key);
        return;
    }

    // 计算按键延迟
    int key_delay = static_cast<int>(
        std::normal_distribution<double>(
            human_params_.key_press_delay_mean,
            human_params_.key_press_delay_variance
        )(rng)
    );
    key_delay = std::max(30, key_delay);

    // 获取当前时间
    auto now = std::chrono::steady_clock::now();
    auto key_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // 如果距离上次按键太近，增加延迟
    long time_since_last_key = key_time - last_key_time_;
    if (time_since_last_key < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50 - time_since_last_key));
    }

    // 按下按键
    sendKeyDown(vk);

    // 短暂停顿
    std::this_thread::sleep_for(std::chrono::milliseconds(key_delay));

    // 释放按键
    sendKeyUp(vk);

    // 更新上次按键时间
    last_key_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void InputSimulator::simulateKeyCombo(const std::vector<std::string>& keys) {
    std::vector<UINT> vk_keys;

    // 转换所有键名为虚拟键代码
    for (const auto& key : keys) {
        UINT vk = mapKeyNameToVirtualKey(key);
        if (vk != 0) {
            vk_keys.push_back(vk);
        } else {
            logWarn_fmt("未知按键被忽略: {}", key);
        }
    }

    if (vk_keys.empty()) {
        logError("没有有效的按键");
        return;
    }

    // 按下所有按键
    for (UINT vk : vk_keys) {
        sendKeyDown(vk);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // 短暂停顿
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 按相反顺序释放所有按键
    for (auto it = vk_keys.rbegin(); it != vk_keys.rend(); ++it) {
        sendKeyUp(*it);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

void InputSimulator::simulateCommonAction(const std::string& action_name) {
    auto it = common_combos.find(action_name);
    if (it != common_combos.end()) {
        simulateKeyCombo(it->second);
    } else {
        logWarn_fmt("未知的常用动作: {}", action_name);
    }
}

void InputSimulator::simulateKeyHold(const std::string& key, int duration_ms) {
    UINT vk = mapKeyNameToVirtualKey(key);
    if (vk == 0) {
        logError_fmt("未知按键: {}", key);
        return;
    }

    // 按下按键
    sendKeyDown(vk);

    // 记录按键状态
    key_states_[vk] = true;

    // 保持按下指定时间
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));

    // 释放按键
    sendKeyUp(vk);

    // 更新按键状态
    key_states_[vk] = false;
}

int InputSimulator::addHumanJitter(int value, int max_jitter) {
    // 添加一个随机偏移，模拟人类输入的不精确性
    double jitter_amount = std::min(static_cast<double>(max_jitter), human_params_.jitter_max);
    std::normal_distribution<> distrib(0, jitter_amount / 2.0);

    return value + static_cast<int>(distrib(rng));
}

UINT InputSimulator::mapKeyNameToVirtualKey(const std::string& key_name) {
    std::string lower_key = key_name;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    auto it = key_map.find(lower_key);
    if (it != key_map.end()) {
        return it->second;
    }

    // 尝试解析单个字符按键
    if (lower_key.length() == 1) {
        return static_cast<UINT>(std::toupper(lower_key[0]));
    }

    return 0;  // 未知按键
}

void InputSimulator::sendInputEvent(INPUT& input_event) {
    if (SendInput(1, &input_event, sizeof(INPUT)) != 1) {
        logError_fmt("发送输入事件失败，错误码: {}", GetLastError());
    }
}

void InputSimulator::sendKeyDown(UINT virtual_key) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtual_key;
    input.ki.dwFlags = 0;  // 按键按下
    sendInputEvent(input);

    // 更新按键状态
    key_states_[virtual_key] = true;
}

void InputSimulator::sendKeyUp(UINT virtual_key) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtual_key;
    input.ki.dwFlags = KEYEVENTF_KEYUP;  // 按键释放
    sendInputEvent(input);

    // 更新按键状态
    key_states_[virtual_key] = false;
}

void InputSimulator::interpolateMouseMovement(int start_x, int start_y, int end_x, int end_y) {
    // 计算移动距离
    int dx = end_x - start_x;
    int dy = end_y - start_y;
    double distance = std::sqrt(dx*dx + dy*dy);

    // 如果距离太小，直接移动
    if (distance < 5) {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dx = static_cast<LONG>(end_x * (65535.0f / GetSystemMetrics(SM_CXSCREEN)));
        input.mi.dy = static_cast<LONG>(end_y * (65535.0f / GetSystemMetrics(SM_CYSCREEN)));
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        sendInputEvent(input);
        return;
    }

    // 根据距离确定步数
    int steps = std::max(5, static_cast<int>(distance / 10.0));

    // 生成移动速度
    double speed = std::normal_distribution<double>(
        human_params_.movement_speed_mean,
        human_params_.movement_speed_variance
    )(rng);
    speed = std::max(5.0, speed);

    // 计算总移动时间
    double total_time_ms = distance / speed * 1000.0;

    // 设置贝塞尔曲线控制点 (模拟人类手腕运动)
    double control_x1 = start_x + dx * 0.3;
    double control_y1 = start_y + dy * 0.1;
    double control_x2 = start_x + dx * 0.7;
    double control_y2 = start_y + dy * 0.9;

    // 添加随机性到控制点
    double curve_randomness = human_params_.curve_randomness;
    control_x1 += dx * curve_randomness * std::normal_distribution<double>(0, 0.1)(rng);
    control_y1 += dy * curve_randomness * std::normal_distribution<double>(0, 0.1)(rng);
    control_x2 += dx * curve_randomness * std::normal_distribution<double>(0, 0.1)(rng);
    control_y2 += dy * curve_randomness * std::normal_distribution<double>(0, 0.1)(rng);

    // 创建贝塞尔曲线点数组
    std::vector<std::pair<double, double>> curve_points;
    for (int i = 0; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;

        // 三次贝塞尔曲线公式
        double x = std::pow(1-t, 3) * start_x +
                  3 * std::pow(1-t, 2) * t * control_x1 +
                  3 * (1-t) * std::pow(t, 2) * control_x2 +
                  std::pow(t, 3) * end_x;

        double y = std::pow(1-t, 3) * start_y +
                  3 * std::pow(1-t, 2) * t * control_y1 +
                  3 * (1-t) * std::pow(t, 2) * control_y2 +
                  std::pow(t, 3) * end_y;

        // 添加细微的人类抖动
        if (i > 0 && i < steps) {
            x += std::normal_distribution<double>(0, human_params_.jitter_max / 3)(rng);
            y += std::normal_distribution<double>(0, human_params_.jitter_max / 3)(rng);
        }

        curve_points.push_back({x, y});
    }

    // 计算每步的移动时间 (模拟加速和减速)
    std::vector<double> step_times;
    double total_time_factor = 0;

    for (int i = 0; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;

        // 加速减速曲线：在中间速度最快，两端速度慢
        double time_factor;
        if (t < 0.3) {
            // 加速阶段
            time_factor = 1.0 - t * human_params_.acceleration_factor;
        } else if (t > 0.7) {
            // 减速阶段
            time_factor = 1.0 + (t - 0.7) * human_params_.deceleration_factor;
        } else {
            // 匀速阶段
            time_factor = 1.0 - 0.3 * human_params_.acceleration_factor;
        }

        step_times.push_back(time_factor);
        total_time_factor += time_factor;
    }

    // 规范化时间因子并计算实际延迟
    std::vector<int> delays;
    for (double factor : step_times) {
        double normalized_factor = factor / total_time_factor;
        int delay_ms = static_cast<int>(total_time_ms * normalized_factor);
        delays.push_back(std::max(1, delay_ms));
    }

    // 执行移动
    for (int i = 0; i <= steps; i++) {
        auto [x, y] = curve_points[i];

        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dx = static_cast<LONG>(x * (65535.0f / GetSystemMetrics(SM_CXSCREEN)));
        input.mi.dy = static_cast<LONG>(y * (65535.0f / GetSystemMetrics(SM_CYSCREEN)));
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        sendInputEvent(input);

        // 等待下一帧
        if (i < steps) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delays[i]));
        }
    }
}