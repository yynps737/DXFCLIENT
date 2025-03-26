#pragma once

#include <windows.h>
#include <string>
#include <vector>

class InputSimulator {
public:
    InputSimulator();
    ~InputSimulator();

    bool initialize();
    void simulateMouseMove(int x, int y, bool smooth = false);
    void simulateMouseClick(int x, int y, bool right_button = false);
    void simulateKeyPress(const std::string& key);
    void simulateKeyCombo(const std::vector<std::string>& keys);
    int addHumanJitter(int value, int max_jitter);

private:
    UINT mapKeyNameToVirtualKey(const std::string& key_name);
    void sendInputEvent(INPUT& input_event);
    void sendKeyDown(UINT virtual_key);
    void sendKeyUp(UINT virtual_key);
    void interpolateMouseMovement(int start_x, int start_y, int end_x, int end_y);

    POINT current_mouse_pos_;
};