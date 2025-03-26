#pragma once

#include <windows.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

// 前向声明
namespace Gdiplus {
    class Bitmap;
    // 不要前向声明Status，它是typedef不是enum
}

// 捕获结果结构体
struct CaptureResult {
    std::vector<uint8_t> jpeg_data;
    int width;
    int height;
    RECT window_rect;
    std::chrono::system_clock::time_point timestamp;
};

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    bool initialize(const std::string& window_title);
    std::shared_ptr<CaptureResult> captureScreen(int quality);
    bool isWindowValid() const;
    RECT getWindowRect() const;

private:
    bool findGameWindow();
    bool captureWindowImage(HBITMAP& bitmap, int& width, int& height);
    std::vector<uint8_t> compressToJpeg(HBITMAP bitmap, int width, int height, int quality);
    void cleanupBitmap(HBITMAP bitmap);

    HWND game_window_;
    HDC window_dc_;
    HDC memory_dc_;
    RECT window_rect_;
    std::string window_title_;
    ULONG_PTR gdiplusToken_;
};