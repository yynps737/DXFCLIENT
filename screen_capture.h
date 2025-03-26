#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <vector>
#include <chrono>

// ǰ������
class DXGIScreenCapture;

// �������ṹ��
struct CaptureResult {
    std::vector<uint8_t> jpeg_data;  // JPEG�����ͼ������
    int width;                       // ���
    int height;                      // �߶�
    RECT window_rect;                // ���ھ���
    std::chrono::system_clock::time_point timestamp;  // ʱ���
    bool changed;                    // �Ƿ������һ֡�����Ա仯
};

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // ��ʼ��������
    bool initialize(const std::string& window_title);

    // ������Ļ
    std::shared_ptr<CaptureResult> captureScreen(int quality);

    // ��鴰���Ƿ���Ч
    bool isWindowValid() const;

    // ��ȡ���ھ���
    RECT getWindowRect() const;

    // ������С�����������룩
    void setMinimumCaptureInterval(int ms) { minimum_capture_interval_ms_ = ms; }

private:
    // ������Ϸ����
    bool findGameWindow();

    // ���񴰿�ͼ��GDI+��ʽ��
    bool captureWindowImage(HBITMAP& bitmap, int& width, int& height);

    // ѹ��ΪJPEG
    std::vector<uint8_t> compressToJpeg(HBITMAP bitmap, int width, int height, int quality);

    // ����λͼ
    void cleanupBitmap(HBITMAP bitmap);

    std::string window_title_;       // ���ڱ���
    HWND game_window_;               // ��Ϸ���ھ��
    HDC window_dc_;                  // ����DC
    HDC memory_dc_;                  // �ڴ�DC
    RECT window_rect_;               // ���ھ���
    ULONG_PTR gdiplusToken_;         // GDI+����

    bool use_dxgi_;                  // �Ƿ�ʹ��DXGI����
    DXGIScreenCapture* dxgi_capture_; // DXGI������

    int64_t last_capture_time_;      // �ϴβ���ʱ��
    int minimum_capture_interval_ms_; // ��С�����������룩

    size_t last_frame_hash_;         // ��һ֡��ϣֵ������֡�����⣩
    std::shared_ptr<CaptureResult> last_capture_result_; // ��һ�β�����
};