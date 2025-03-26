#define NOMINMAX  // 添加在所有包含文件之前
#include <windows.h>
#include "input_simulator.h"
#include "spdlog/spdlog.h"
#include <cmath>
#include <random>
#include <chrono>


// 随机数生成器
std::random_device rd;
std::mt19937 gen(rd());

InputSimulator::InputSimulator() {
    // 初始化位置
    GetCursorPos(&last_mouse_pos_);
}

InputSimulator::~InputSimulator() {
    // 无需特殊清理
}

bool InputSimulator::initialize() {
    // 初始化键盘映射
    key_map_["escape"] = VK_ESCAPE;
    key_map_["space"] = VK_SPACE;
    key_map_["enter"] = VK_RETURN;
    key_map_["tab"] = VK_TAB;

    // 方向键
    key_map_["up"] = VK_UP;
    key_map_["down"] = VK_DOWN;
    key_map_["left"] = VK_LEFT;
    key_map_["right"] = VK_RIGHT;

    // 功能键
    key_map_["f1"] = VK_F1;
    key_map_["f2"] = VK_F2;
    key_map_["f3"] = VK_F3;
    key_map_["f4"] = VK_F4;
    key_map_["f5"] = VK_F5;
    key_map_["f6"] = VK_F6;
    key_map_["f7"] = VK_F7;
    key_map_["f8"] = VK_F8;
    key_map_["f9"] = VK_F9;
    key_map_["f10"] = VK_F10;
    key_map_["f11"] = VK_F11;
    key_map_["f12"] = VK_F12;

    // 修饰键
    key_map_["shift"] = VK_SHIFT;
    key_map_["ctrl"] = VK_CONTROL;
    key_map_["alt"] = VK_MENU;

    // 数字键和字母键
    for (char c = '0'; c <= '9'; c++) {
        std::string key_name(1, c);
        key_map_[key_name] = c;
    }

    for (char c = 'a'; c <= 'z'; c++) {
        std::string key_name(1, c);
        key_map_[key_name] = toupper(c);  // 虚拟键代码使用大写字母
    }

    // 其他常用键
    key_map_["backspace"] = VK_BACK;
    key_map_["delete"] = VK_DELETE;
    key_map_["insert"] = VK_INSERT;
    key_map_["home"] = VK_HOME;
    key_map_["end"] = VK_END;
    key_map_["pageup"] = VK_PRIOR;
    key_map_["pagedown"] = VK_NEXT;

    spdlog::info("输入模拟器初始化完成");
    return true;
}

void InputSimulator::simulateKeyPress(const std::string& key) {
    // 查找键代码
    auto it = key_map_.find(key);
    if (it == key_map_.end()) {
        spdlog::warn("未知键: {}", key);
        return;
    }

    int vk = it->second;

    // 模拟按键
    INPUT input[2] = { 0 };

    // 按下
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = vk;

    // 释放
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = vk;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;

    // 发送输入
    UINT result = SendInput(2, input, sizeof(INPUT));
    if (result != 2) {
        spdlog::error("模拟按键失败: {}", key);
    }

    // 添加人类化延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomDelay(50, 150)));
}

void InputSimulator::simulateKeyCombo(const std::vector<std::string>& keys) {
    if (keys.empty()) {
        return;
    }

    // 准备按键输入
    std::vector<INPUT> inputs;
    std::vector<int> vk_codes;

    // 收集所有有效的虚拟键代码
    for (const auto& key : keys) {
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            vk_codes.push_back(it->second);
        }
        else {
            spdlog::warn("未知键: {}", key);
        }
    }

    if (vk_codes.empty()) {
        return;
    }

    // 按下所有键
    for (int vk : vk_codes) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        inputs.push_back(input);
    }

    // 释放所有键（逆序）
    for (auto it = vk_codes.rbegin(); it != vk_codes.rend(); ++it) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = *it;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }

    // 发送输入
    UINT result = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (result != inputs.size()) {
        spdlog::error("模拟组合键失败");
    }

    // 添加人类化延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomDelay(80, 200)));
}

void InputSimulator::simulateMouseMove(int x, int y, bool smooth) {
    POINT current_pos;
    GetCursorPos(&current_pos);

    if (smooth) {
        // 生成平滑路径
        std::vector<POINT> path = generateHumanMousePath(current_pos, { x, y });

        // 沿路径移动
        for (const auto& point : path) {
            SetCursorPos(point.x, point.y);
            std::this_thread::sleep_for(std::chrono::milliseconds(getRandomDelay(2, 10)));
        }
    }
    else {
        // 直接跳到目标位置
        SetCursorPos(x, y);
    }

    // 更新记录的位置
    last_mouse_pos_.x = x;
    last_mouse_pos_.y = y;
}

void InputSimulator::simulateMouseClick(int x, int y, bool right_button) {
    // 先移动到位置
    simulateMouseMove(x, y);

    // 准备鼠标输入
    INPUT input[2] = { 0 };

    // 按下
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = right_button ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;

    // 释放
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = right_button ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;

    // 发送输入
    UINT result = SendInput(2, input, sizeof(INPUT));
    if (result != 2) {
        spdlog::error("模拟鼠标点击失败");
    }

    // 模拟人类化的点击后暂停
    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomDelay(50, 200)));
}

void InputSimulator::simulateMouseDoubleClick(int x, int y) {
    // 进行两次点击操作，间隔时间模拟双击
    simulateMouseClick(x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomDelay(40, 80)));
    simulateMouseClick(x, y);
}

// 生成人类化鼠标轨迹
std::vector<POINT> InputSimulator::generateHumanMousePath(POINT start, POINT end, int steps) {
    std::vector<POINT> path;

    // 计算距离
    double distance = std::sqrt(std::pow(end.x - start.x, 2) + std::pow(end.y - start.y, 2));

    // 根据距离调整步数
    if (steps <= 0) {
        steps = static_cast<int>(distance / 10.0);
        steps = std::max(5, std::min(50, steps)); // 限制在合理范围内
    }

    // 生成路径点
    for (int i = 0; i < steps; i++) {
        double t = static_cast<double>(i) / (steps - 1);

        // Bezier曲线控制点（添加随机偏移使曲线更自然）
        double cp_x = start.x + (end.x - start.x) * 0.5 + addHumanJitter(0, 10);
        double cp_y = start.y + (end.y - start.y) * 0.5 + addHumanJitter(0, 10);

        // 二次Bezier曲线
        double x = std::pow(1 - t, 2) * start.x + 2 * (1 - t) * t * cp_x + std::pow(t, 2) * end.x;
        double y = std::pow(1 - t, 2) * start.y + 2 * (1 - t) * t * cp_y + std::pow(t, 2) * end.y;

        // 添加微小抖动
        x += addHumanJitter(0, 1);
        y += addHumanJitter(0, 1);

        POINT p = { static_cast<LONG>(x), static_cast<LONG>(y) };
        path.push_back(p);
    }

    // 确保最后一个点是目标位置
    path.back() = end;

    return path;
}

// 添加人类化抖动
int InputSimulator::addHumanJitter(int value, int range) {
    std::uniform_int_distribution<> dist(-range, range);
    return value + dist(gen);
}

// 生成随机延迟
int InputSimulator::getRandomDelay(int min_ms, int max_ms) {
    std::uniform_int_distribution<> dist(min_ms, max_ms);
    return dist(gen);
}