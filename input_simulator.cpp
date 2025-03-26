#include "input_simulator.h"
#include "LogWrapper.h"
#include <map>
#include <random>
#include <algorithm>
#include <chrono>
#include <thread>

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
    {"backspace", VK_BACK}, {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}
};

InputSimulator::InputSimulator() {
    // 获取当前鼠标位置
    GetCursorPos(&current_mouse_pos_);
}

InputSimulator::~InputSimulator() {}

bool InputSimulator::initialize() {
    logInfo("输入模拟器初始化");
    return true;
}

void InputSimulator::simulateMouseMove(int x, int y, bool smooth) {
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

    // 然后点击
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = right_button ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    sendInputEvent(input);

    // 模拟人类点击的短暂停顿
    std::this_thread::sleep_for(std::chrono::milliseconds(50 + (std::rand() % 50)));

    // 释放鼠标按钮
    input.mi.dwFlags = right_button ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    sendInputEvent(input);
}

void InputSimulator::simulateKeyPress(const std::string& key) {
    UINT vk = mapKeyNameToVirtualKey(key);
    if (vk == 0) {
        logError_fmt("未知按键: {}", key);
        return;
    }

    // 按下按键
    sendKeyDown(vk);

    // 短暂停顿
    std::this_thread::sleep_for(std::chrono::milliseconds(50 + (std::rand() % 30)));

    // 释放按键
    sendKeyUp(vk);
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

int InputSimulator::addHumanJitter(int value, int max_jitter) {
    // 添加一个随机偏移，模拟人类输入的不精确性
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-max_jitter, max_jitter);

    return value + distrib(gen);
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
}

void InputSimulator::sendKeyUp(UINT virtual_key) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtual_key;
    input.ki.dwFlags = KEYEVENTF_KEYUP;  // 按键释放
    sendInputEvent(input);
}

void InputSimulator::interpolateMouseMovement(int start_x, int start_y, int end_x, int end_y) {
    // 计算移动距离
    int dx = end_x - start_x;
    int dy = end_y - start_y;
    double distance = std::sqrt(dx*dx + dy*dy);

    // 根据距离确定步数
    int steps = std::max(10, static_cast<int>(distance / 10.0));

    // 添加一些随机性，使移动看起来更自然
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> speed_distrib(15.0, 5.0);  // 移动速度分布

    for (int i = 1; i <= steps; ++i) {
        // 计算当前步的位置
        int x = start_x + static_cast<int>((dx * i) / steps);
        int y = start_y + static_cast<int>((dy * i) / steps);

        // 添加一些微小抖动
        if (i != steps) {  // 不在最后一步添加抖动
            std::uniform_int_distribution<> jitter(-2, 2);
            x += jitter(gen);
            y += jitter(gen);
        }

        // 移动鼠标
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dx = static_cast<LONG>(x * (65535.0f / GetSystemMetrics(SM_CXSCREEN)));
        input.mi.dy = static_cast<LONG>(y * (65535.0f / GetSystemMetrics(SM_CYSCREEN)));
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        sendInputEvent(input);

        // 模拟人类移动的变速
        double speed = std::max(5.0, speed_distrib(gen));
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000.0 / speed)));
    }
}