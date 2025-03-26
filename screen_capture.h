#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <vector>
#include <chrono>

// 前向声明
class DXGIScreenCapture;

// 捕获结果结构体
struct CaptureResult {
    std::vector<uint8_t> jpeg_data;  // JPEG编码的图像数据
    int width;                       // 宽度
    int height;                      // 高度
    RECT window_rect;                // 窗口矩形
    std::chrono::system_clock::time_point timestamp;  // 时间戳
    bool changed;                    // 是否相对上一帧有明显变化
};

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // 初始化捕获器
    bool initialize(const std::string& window_title);

    // 捕获屏幕
    std::shared_ptr<CaptureResult> captureScreen(int quality);

    // 检查窗口是否有效
    bool isWindowValid() const;

    // 获取窗口矩形
    RECT getWindowRect() const;

    // 设置最小捕获间隔（毫秒）
    void setMinimumCaptureInterval(int ms) { minimum_capture_interval_ms_ = ms; }

private:
    // 查找游戏窗口
    bool findGameWindow();

    // 捕获窗口图像（GDI+方式）
    bool captureWindowImage(HBITMAP& bitmap, int& width, int& height);

    // 压缩为JPEG
    std::vector<uint8_t> compressToJpeg(HBITMAP bitmap, int width, int height, int quality);

    // 清理位图
    void cleanupBitmap(HBITMAP bitmap);

    std::string window_title_;       // 窗口标题
    HWND game_window_;               // 游戏窗口句柄
    HDC window_dc_;                  // 窗口DC
    HDC memory_dc_;                  // 内存DC
    RECT window_rect_;               // 窗口矩形
    ULONG_PTR gdiplusToken_;         // GDI+令牌

    bool use_dxgi_;                  // 是否使用DXGI捕获
    DXGIScreenCapture* dxgi_capture_; // DXGI捕获器

    int64_t last_capture_time_;      // 上次捕获时间
    int minimum_capture_interval_ms_; // 最小捕获间隔（毫秒）

    size_t last_frame_hash_;         // 上一帧哈希值（用于帧差异检测）
    std::shared_ptr<CaptureResult> last_capture_result_; // 上一次捕获结果
};