#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>

// 人类行为模拟参数结构体
struct HumanParameters {
    double movement_speed_mean;       // 平均移动速度 (像素/帧)
    double movement_speed_variance;   // 速度变化
    double click_delay_mean;          // 平均点击延迟 (毫秒)
    double click_delay_variance;      // 点击延迟变化
    double double_click_threshold;    // 双击阈值 (毫秒)
    double key_press_delay_mean;      // 按键按下延迟 (毫秒)
    double key_press_delay_variance;  // 按键延迟变化
    double fatigue_factor;            // 疲劳因子 (1.0 = 正常)
    double jitter_max;                // 最大抖动 (像素)
    double acceleration_factor;       // 加速因子
    double deceleration_factor;       // 减速因子
    double curve_randomness;          // 曲线随机性
    bool gaming_mode;                 // 游戏模式
};

class InputSimulator {
public:
    InputSimulator();
    ~InputSimulator();

    // 初始化模拟器
    bool initialize();

    // 鼠标操作
    void simulateMouseMove(int x, int y, bool smooth = false);
    void simulateMouseClick(int x, int y, bool right_button = false);
    void simulateDoubleClick(int x, int y, bool right_button = false);
    void simulateMouseDrag(int start_x, int start_y, int end_x, int end_y);

    // 键盘操作
    void simulateKeyPress(const std::string& key);
    void simulateKeyCombo(const std::vector<std::string>& keys);
    void simulateCommonAction(const std::string& action_name);
    void simulateKeyHold(const std::string& key, int duration_ms);

    // 辅助函数
    int addHumanJitter(int value, int max_jitter);

    // 释放所有按键
    void releaseAllKeys();

    // 人类行为参数设置
    void setHumanParameters(const HumanParameters& params) { human_params_ = params; }
    HumanParameters getHumanParameters() const { return human_params_; }
    void resetHumanSession() { session_start_time_ = std::chrono::steady_clock::now(); }

private:
    UINT mapKeyNameToVirtualKey(const std::string& key_name);
    void sendInputEvent(INPUT& input_event);
    void sendKeyDown(UINT virtual_key);
    void sendKeyUp(UINT virtual_key);
    void interpolateMouseMovement(int start_x, int start_y, int end_x, int end_y);

    // 人类行为模拟
    void initializeHumanParameters();
    void updateHumanParameters();

    POINT current_mouse_pos_;
    bool initialized_ = false;

    // 按键状态跟踪
    std::unordered_map<UINT, bool> key_states_;

    // 时间跟踪 (用于模拟真实人类输入)
    long last_click_time_;  // 上次点击时间
    long last_key_time_;    // 上次按键时间

    // 人类行为参数
    HumanParameters human_params_;

    // 会话开始时间 (用于模拟疲劳)
    std::chrono::steady_clock::time_point session_start_time_;
};