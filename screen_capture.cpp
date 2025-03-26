#include <windows.h>
#include <objbase.h> // 提供PROPID定义

// 如果PROPID仍未定义，手动定义它
#ifndef PROPID
typedef ULONG PROPID;
#endif

// 包含GDI+头文件
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// 然后包含其他头文件
#include "LogWrapper.h"
#include <memory>
#include <algorithm>
#include <objidl.h>
#include "screen_capture.h"

using namespace Gdiplus;

// RAII包装器GDI+
class GdiplusInitializer {
public:
    GdiplusInitializer() {
        GdiplusStartupInput gdiplusStartupInput;
        Status status = GdiplusStartup(&token_, &gdiplusStartupInput, NULL);
        if (status != Ok) {
            logError_fmt("GDI+初始化失败: {}", static_cast<int>(status));
            initialized_ = false;
        }
        else {
            initialized_ = true;
        }
    }

    ~GdiplusInitializer() {
        if (initialized_) {
            GdiplusShutdown(token_);
        }
    }

    bool isInitialized() const { return initialized_; }
    ULONG_PTR getToken() const { return token_; }

private:
    ULONG_PTR token_;
    bool initialized_;
};

// 静态GDI+初始化器
static GdiplusInitializer gdiplusInit;

// 帮助函数获取JPEG编码器CLSID
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;          // 编码器数量
    UINT size = 0;         // 编码器信息大小

    // 获取图像编码器信息
    GetImageEncodersSize(&num, &size);
    if (size == 0) {
        return -1;
    }

    // 分配内存
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) {
        return -1;
    }

    // 获取编码器信息
    GetImageEncoders(num, size, pImageCodecInfo);

    // 查找指定格式的编码器
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(pImageCodecInfo[i].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[i].Clsid;
            free(pImageCodecInfo);
            return i;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

ScreenCapture::ScreenCapture() : game_window_(NULL), window_dc_(NULL), memory_dc_(NULL) {
    if (!gdiplusInit.isInitialized()) {
        logError("GDI+初始化失败，屏幕捕获将不工作");
    }
    gdiplusToken_ = gdiplusInit.getToken();
}

ScreenCapture::~ScreenCapture() {
    // 清理资源
    if (memory_dc_) {
        DeleteDC(memory_dc_);
    }
    if (window_dc_) {
        ReleaseDC(game_window_, window_dc_);
    }
}

bool ScreenCapture::initialize(const std::string& window_title) {
    window_title_ = window_title;

    // 查找游戏窗口
    if (!findGameWindow()) {
        logError_fmt("找不到游戏窗口: {}", window_title);
        return false;
    }

    // 获取窗口DC
    window_dc_ = GetDC(game_window_);
    if (!window_dc_) {
        logError("获取窗口DC失败");
        return false;
    }

    // 创建内存DC
    memory_dc_ = CreateCompatibleDC(window_dc_);
    if (!memory_dc_) {
        logError("创建内存DC失败");
        ReleaseDC(game_window_, window_dc_);
        window_dc_ = NULL;
        return false;
    }

    logInfo_fmt("屏幕捕获初始化成功，目标窗口: {}", window_title);
    return true;
}

bool ScreenCapture::findGameWindow() {
    // 查找游戏窗口
    game_window_ = FindWindowA(NULL, window_title_.c_str());
    if (!game_window_) {
        // 尝试使用部分标题匹配
        game_window_ = FindWindowA(NULL, NULL);
        char window_text[256];

        while (game_window_) {
            GetWindowTextA(game_window_, window_text, sizeof(window_text));
            if (strstr(window_text, window_title_.c_str()) != nullptr) {
                break;
            }
            game_window_ = FindWindowEx(NULL, game_window_, NULL, NULL);
        }
    }

    if (!game_window_) {
        return false;
    }

    // 获取窗口矩形
    GetWindowRect(game_window_, &window_rect_);

    // 检查窗口是否可见
    if (!IsWindowVisible(game_window_)) {
        logError("游戏窗口不可见");
        game_window_ = NULL;
        return false;
    }

    logInfo_fmt("找到游戏窗口，句柄: {}, 大小: {}x{}",
        reinterpret_cast<uintptr_t>(game_window_),
        window_rect_.right - window_rect_.left,
        window_rect_.bottom - window_rect_.top);

    return true;
}

std::shared_ptr<CaptureResult> ScreenCapture::captureScreen(int quality) {
    // 检查窗口是否有效
    if (!isWindowValid()) {
        logError("无效的游戏窗口");
        return nullptr;
    }

    // 更新窗口矩形
    GetWindowRect(game_window_, &window_rect_);

    // 计算窗口大小
    int width = window_rect_.right - window_rect_.left;
    int height = window_rect_.bottom - window_rect_.top;

    // 创建位图
    HBITMAP bitmap = NULL;
    if (!captureWindowImage(bitmap, width, height)) {
        logError("捕获窗口图像失败");
        return nullptr;
    }

    // 压缩为JPEG
    std::vector<uint8_t> jpeg_data = compressToJpeg(bitmap, width, height, quality);

    // 清理位图
    cleanupBitmap(bitmap);

    if (jpeg_data.empty()) {
        logError("JPEG压缩失败");
        return nullptr;
    }

    // 创建结果
    auto result = std::make_shared<CaptureResult>();
    result->jpeg_data = std::move(jpeg_data);
    result->width = width;
    result->height = height;
    result->window_rect = window_rect_;
    result->timestamp = std::chrono::system_clock::now();

    return result;
}

bool ScreenCapture::captureWindowImage(HBITMAP& bitmap, int& width, int& height) {
    // 检查窗口是否最小化
    if (IsIconic(game_window_)) {
        logWarn("游戏窗口已最小化，无法捕获");
        return false;
    }

    // 创建兼容位图
    bitmap = CreateCompatibleBitmap(window_dc_, width, height);
    if (!bitmap) {
        logError("创建兼容位图失败");
        return false;
    }

    // 选择位图到内存DC
    HBITMAP old_bitmap = (HBITMAP)SelectObject(memory_dc_, bitmap);

    // 复制窗口内容到位图
    BOOL result = PrintWindow(game_window_, memory_dc_, PW_CLIENTONLY);

    if (!result) {
        // 如果PrintWindow失败，尝试使用BitBlt
        result = BitBlt(memory_dc_, 0, 0, width, height, window_dc_, 0, 0, SRCCOPY);
    }

    // 恢复旧位图
    SelectObject(memory_dc_, old_bitmap);

    if (!result) {
        logError("捕获窗口内容失败");
        DeleteObject(bitmap);
        bitmap = NULL;
        return false;
    }

    return true;
}

std::vector<uint8_t> ScreenCapture::compressToJpeg(HBITMAP bitmap, int width, int height, int quality) {
    // 创建Bitmap对象
    Bitmap* bmp = new Bitmap(bitmap, NULL);
    if (!bmp) {
        logError("创建GDI+ Bitmap失败");
        return {};
    }

    // 检查位图是否有效
    if (bmp->GetLastStatus() != Ok) {
        logError("GDI+ Bitmap无效");
        delete bmp;
        return {};
    }

    // 创建内存流
    IStream* istream = nullptr;
    CreateStreamOnHGlobal(NULL, TRUE, &istream);

    // 获取JPEG编码器
    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);

    // 设置编码参数
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG qualityValue = quality;
    encoderParams.Parameter[0].Value = &qualityValue;

    // 保存为JPEG
    Status status = bmp->Save(istream, &jpegClsid, &encoderParams);

    // 清理位图
    delete bmp;

    if (status != Ok) {
        logError_fmt("保存JPEG失败: {}", static_cast<int>(status));
        istream->Release();
        return {};
    }

    // 获取数据大小
    STATSTG stat;
    istream->Stat(&stat, STATFLAG_NONAME);
    ULONG size = stat.cbSize.LowPart;

    // 分配内存
    std::vector<uint8_t> buffer(size);

    // 重置位置
    LARGE_INTEGER li = { 0 };
    istream->Seek(li, STREAM_SEEK_SET, NULL);

    // 读取数据
    ULONG bytesRead;
    istream->Read(buffer.data(), size, &bytesRead);

    // 释放流
    istream->Release();

    return buffer;
}

void ScreenCapture::cleanupBitmap(HBITMAP bitmap) {
    if (bitmap) {
        DeleteObject(bitmap);
    }
}

bool ScreenCapture::isWindowValid() const {
    return game_window_ != NULL && IsWindow(game_window_);
}

RECT ScreenCapture::getWindowRect() const {
    return window_rect_;
}