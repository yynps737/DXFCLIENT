#pragma once

#include <windows.h>
#include <string>
#include <chrono>  // ÃÌº”’‚––
#include <vector>
#include <memory>

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
    std::shared_ptr<CaptureResult> captureScreen(int quality = 80);
    bool isWindowValid() const;
    RECT getWindowRect() const;

private:
    bool findGameWindow();
    bool captureWindowImage(HBITMAP& bitmap, int& width, int& height);
    std::vector<uint8_t> compressToJpeg(HBITMAP bitmap, int width, int height, int quality);
    void cleanupBitmap(HBITMAP bitmap);

    HWND game_window_;
    std::string window_title_;
    RECT window_rect_;
    HDC window_dc_;
    HDC memory_dc_;
    ULONG_PTR gdiplusToken_;
};