#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

struct Action {
    std::string type;
    std::string key;
    std::vector<std::string> keys;
    std::vector<float> position;
    float delay;
    std::string purpose;
    std::string description;
    // �����ֶ�...
};

class InputSimulator {
public:
    InputSimulator();
    ~InputSimulator();

    bool initialize();
    void simulateKeyPress(const std::string& key);
    void simulateKeyCombo(const std::vector<std::string>& keys);
    void simulateMouseMove(int x, int y, bool smooth = true);
    void simulateMouseClick(int x, int y, bool right_button = false);
    void simulateMouseDoubleClick(int x, int y);

    // ���������Ϊģ��
    std::vector<POINT> generateHumanMousePath(POINT start, POINT end, int steps = 20);
    int addHumanJitter(int value, int range = 2);
    int getRandomDelay(int min_ms = 20, int max_ms = 100);

private:
    // ����������������ӳ��
    std::unordered_map<std::string, int> key_map_;
    POINT last_mouse_pos_;
};
