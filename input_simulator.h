#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>

// ������Ϊģ������ṹ��
struct HumanParameters {
    double movement_speed_mean;       // ƽ���ƶ��ٶ� (����/֡)
    double movement_speed_variance;   // �ٶȱ仯
    double click_delay_mean;          // ƽ������ӳ� (����)
    double click_delay_variance;      // ����ӳٱ仯
    double double_click_threshold;    // ˫����ֵ (����)
    double key_press_delay_mean;      // ���������ӳ� (����)
    double key_press_delay_variance;  // �����ӳٱ仯
    double fatigue_factor;            // ƣ������ (1.0 = ����)
    double jitter_max;                // ��󶶶� (����)
    double acceleration_factor;       // ��������
    double deceleration_factor;       // ��������
    double curve_randomness;          // ���������
    bool gaming_mode;                 // ��Ϸģʽ
};

class InputSimulator {
public:
    InputSimulator();
    ~InputSimulator();

    // ��ʼ��ģ����
    bool initialize();

    // ������
    void simulateMouseMove(int x, int y, bool smooth = false);
    void simulateMouseClick(int x, int y, bool right_button = false);
    void simulateDoubleClick(int x, int y, bool right_button = false);
    void simulateMouseDrag(int start_x, int start_y, int end_x, int end_y);

    // ���̲���
    void simulateKeyPress(const std::string& key);
    void simulateKeyCombo(const std::vector<std::string>& keys);
    void simulateCommonAction(const std::string& action_name);
    void simulateKeyHold(const std::string& key, int duration_ms);

    // ��������
    int addHumanJitter(int value, int max_jitter);

    // �ͷ����а���
    void releaseAllKeys();

    // ������Ϊ��������
    void setHumanParameters(const HumanParameters& params) { human_params_ = params; }
    HumanParameters getHumanParameters() const { return human_params_; }
    void resetHumanSession() { session_start_time_ = std::chrono::steady_clock::now(); }

private:
    UINT mapKeyNameToVirtualKey(const std::string& key_name);
    void sendInputEvent(INPUT& input_event);
    void sendKeyDown(UINT virtual_key);
    void sendKeyUp(UINT virtual_key);
    void interpolateMouseMovement(int start_x, int start_y, int end_x, int end_y);

    // ������Ϊģ��
    void initializeHumanParameters();
    void updateHumanParameters();

    POINT current_mouse_pos_;
    bool initialized_ = false;

    // ����״̬����
    std::unordered_map<UINT, bool> key_states_;

    // ʱ����� (����ģ����ʵ��������)
    long last_click_time_;  // �ϴε��ʱ��
    long last_key_time_;    // �ϴΰ���ʱ��

    // ������Ϊ����
    HumanParameters human_params_;

    // �Ự��ʼʱ�� (����ģ��ƣ��)
    std::chrono::steady_clock::time_point session_start_time_;
};