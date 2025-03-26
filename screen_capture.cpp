#include <windows.h>
#include <objbase.h> // 提供PROPID定义

// 如果PROPID仍未定义，手动定义它
#ifndef PROPID
typedef ULONG PROPID;
#endif

// 包含GDI+头文件
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// DirectX截图相关头文件
#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// 然后包含其他头文件
#include "LogWrapper.h"
#include <memory>
#include <algorithm>
#include <chrono>
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
    ImageCodecInfo* pImageCodecInfo = static_cast<ImageCodecInfo*>(malloc(size));
    if (pImageCodecInfo == NULL) {
        return -1;
    }

    // 获取编码器信息
    GetImageEncoders(num, size, pImageCodecInfo);

    // 查找指定格式的编码器
    int result = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(pImageCodecInfo[i].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[i].Clsid;
            result = i;
            break;
        }
    }

    free(pImageCodecInfo);
    return result;
}

// 使用DXGI进行屏幕捕获的类
class DXGIScreenCapture {
public:
    DXGIScreenCapture() : initialized_(false) {}

    ~DXGIScreenCapture() {
        if (initialized_) {
            cleanup();
        }
    }

    bool initialize() {
        // 创建D3D11设备
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &d3dDevice_, &featureLevel, &d3dContext_);

        if (FAILED(hr)) {
            logError_fmt("创建D3D11设备失败: 0x{:X}", hr);
            return false;
        }

        // 获取DXGI设备
        hr = d3dDevice_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice_));
        if (FAILED(hr)) {
            logError_fmt("获取DXGI设备失败: 0x{:X}", hr);
            cleanup();
            return false;
        }

        // 获取DXGI适配器
        hr = dxgiDevice_->GetAdapter(&dxgiAdapter_);
        if (FAILED(hr)) {
            logError_fmt("获取DXGI适配器失败: 0x{:X}", hr);
            cleanup();
            return false;
        }

        // 获取主显示输出
        hr = dxgiAdapter_->EnumOutputs(0, &dxgiOutput_);
        if (FAILED(hr)) {
            logError_fmt("获取DXGI输出失败: 0x{:X}", hr);
            cleanup();
            return false;
        }

        // 获取DXGI输出1接口
        hr = dxgiOutput_->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&dxgiOutput1_));
        if (FAILED(hr)) {
            logError_fmt("获取DXGI输出1失败: 0x{:X}", hr);
            cleanup();
            return false;
        }

        initialized_ = true;
        return true;
    }

    std::vector<uint8_t> captureScreen(const RECT& targetRect, int quality) {
        if (!initialized_ && !initialize()) {
            return {};
        }

        try {
            // 计算捕获区域尺寸
            int width = targetRect.right - targetRect.left;
            int height = targetRect.bottom - targetRect.top;

            // 创建输出复制器
            IDXGIOutputDuplication* duplication = nullptr;
            HRESULT hr = dxgiOutput1_->DuplicateOutput(d3dDevice_, &duplication);
            if (FAILED(hr)) {
                logError_fmt("创建输出复制器失败: 0x{:X}", hr);
                return {};
            }

            // 创建纹理描述
            D3D11_TEXTURE2D_DESC texDesc;
            ZeroMemory(&texDesc, sizeof(texDesc));
            texDesc.Width = width;
            texDesc.Height = height;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_STAGING;
            texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            texDesc.MiscFlags = 0;

            // 创建暂存纹理
            ID3D11Texture2D* stagingTexture = nullptr;
            hr = d3dDevice_->CreateTexture2D(&texDesc, nullptr, &stagingTexture);
            if (FAILED(hr)) {
                logError_fmt("创建暂存纹理失败: 0x{:X}", hr);
                duplication->Release();
                return {};
            }

            // 等待下一帧
            IDXGIResource* desktopResource = nullptr;
            DXGI_OUTDUPL_FRAME_INFO frameInfo;

            // 尝试获取帧，超时设为100ms
            hr = duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                logWarn("获取帧超时");
                stagingTexture->Release();
                duplication->Release();
                return {};
            }
            else if (FAILED(hr)) {
                logError_fmt("获取帧失败: 0x{:X}", hr);
                stagingTexture->Release();
                duplication->Release();
                return {};
            }

            // 获取桌面纹理
            ID3D11Texture2D* desktopTexture = nullptr;
            hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&desktopTexture));
            if (FAILED(hr)) {
                logError_fmt("查询桌面纹理接口失败: 0x{:X}", hr);
                desktopResource->Release();
                stagingTexture->Release();
                duplication->ReleaseFrame();
                duplication->Release();
                return {};
            }

            // 复制区域到暂存纹理
            D3D11_BOX sourceRegion;
            sourceRegion.left = targetRect.left;
            sourceRegion.top = targetRect.top;
            sourceRegion.right = targetRect.right;
            sourceRegion.bottom = targetRect.bottom;
            sourceRegion.front = 0;
            sourceRegion.back = 1;

            d3dContext_->CopySubresourceRegion(stagingTexture, 0, 0, 0, 0, desktopTexture, 0, &sourceRegion);

            // 映射暂存纹理
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = d3dContext_->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
            if (FAILED(hr)) {
                logError_fmt("映射纹理失败: 0x{:X}", hr);
                desktopTexture->Release();
                desktopResource->Release();
                stagingTexture->Release();
                duplication->ReleaseFrame();
                duplication->Release();
                return {};
            }

            // 创建位图
            Bitmap bitmap(width, height, mappedResource.RowPitch, PixelFormat32bppARGB, static_cast<BYTE*>(mappedResource.pData));

            // 释放映射
            d3dContext_->Unmap(stagingTexture, 0);

            // 释放资源
            desktopTexture->Release();
            desktopResource->Release();
            duplication->ReleaseFrame();
            duplication->Release();
            stagingTexture->Release();

            // 压缩为JPEG
            std::vector<uint8_t> jpegData;
            saveToJpeg(bitmap, jpegData, quality);

            return jpegData;
        }
        catch (std::exception& e) {
            logError_fmt("捕获屏幕时异常: {}", e.what());
            return {};
        }
    }

private:
    bool saveToJpeg(Bitmap& bitmap, std::vector<uint8_t>& output, int quality) {
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

        // 创建内存流
        IStream* stream = nullptr;
        CreateStreamOnHGlobal(NULL, TRUE, &stream);

        // 保存到流
        Status status = bitmap.Save(stream, &jpegClsid, &encoderParams);
        if (status != Ok) {
            logError_fmt("保存JPEG失败: {}", static_cast<int>(status));
            stream->Release();
            return false;
        }

        // 获取数据大小
        STATSTG stat;
        stream->Stat(&stat, STATFLAG_NONAME);
        ULONG size = stat.cbSize.LowPart;

        // 重置流位置
        LARGE_INTEGER li = { 0 };
        stream->Seek(li, STREAM_SEEK_SET, NULL);

        // 读取数据
        output.resize(size);
        ULONG bytesRead = 0;
        stream->Read(output.data(), size, &bytesRead);

        // 释放流
        stream->Release();
        return true;
    }

    void cleanup() {
        if (dxgiOutput1_) {
            dxgiOutput1_->Release();
            dxgiOutput1_ = nullptr;
        }
        if (dxgiOutput_) {
            dxgiOutput_->Release();
            dxgiOutput_ = nullptr;
        }
        if (dxgiAdapter_) {
            dxgiAdapter_->Release();
            dxgiAdapter_ = nullptr;
        }
        if (dxgiDevice_) {
            dxgiDevice_->Release();
            dxgiDevice_ = nullptr;
        }
        if (d3dContext_) {
            d3dContext_->Release();
            d3dContext_ = nullptr;
        }
        if (d3dDevice_) {
            d3dDevice_->Release();
            d3dDevice_ = nullptr;
        }
        initialized_ = false;
    }

    bool initialized_;
    ID3D11Device* d3dDevice_ = nullptr;
    ID3D11DeviceContext* d3dContext_ = nullptr;
    IDXGIDevice* dxgiDevice_ = nullptr;
    IDXGIAdapter* dxgiAdapter_ = nullptr;
    IDXGIOutput* dxgiOutput_ = nullptr;
    IDXGIOutput1* dxgiOutput1_ = nullptr;
};

ScreenCapture::ScreenCapture() : game_window_(NULL), window_dc_(NULL), memory_dc_(NULL), use_dxgi_(false), last_capture_time_(0) {
    if (!gdiplusInit.isInitialized()) {
        logError("GDI+初始化失败，屏幕捕获将不工作");
    }
    gdiplusToken_ = gdiplusInit.getToken();

    // 尝试初始化DXGI捕获
    dxgi_capture_ = new DXGIScreenCapture();
    if (dxgi_capture_->initialize()) {
        use_dxgi_ = true;
        logInfo("已启用DXGI屏幕捕获");
    }
    else {
        logInfo("DXGI屏幕捕获初始化失败，将使用GDI+捕获");
    }

    // 初始化差异检测
    last_frame_hash_ = 0;
    minimum_capture_interval_ms_ = 50; // 最小捕获间隔，避免过于频繁
}

ScreenCapture::~ScreenCapture() {
    // 清理资源
    if (memory_dc_) {
        DeleteDC(memory_dc_);
    }
    if (window_dc_) {
        ReleaseDC(game_window_, window_dc_);
    }
    if (dxgi_capture_) {
        delete dxgi_capture_;
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
    // 清除之前的窗口句柄
    game_window_ = NULL;

    // 查找游戏窗口 - 先尝试精确匹配
    game_window_ = FindWindowA(NULL, window_title_.c_str());
    if (game_window_) {
        logInfo_fmt("找到精确匹配窗口: {}", window_title_);
    }
    else {
        // 尝试部分匹配
        HWND hwnd = FindWindowA(NULL, NULL);
        char window_text[256];

        while (hwnd) {
            GetWindowTextA(hwnd, window_text, sizeof(window_text));
            if (strstr(window_text, window_title_.c_str()) != nullptr) {
                game_window_ = hwnd;
                logInfo_fmt("找到部分匹配窗口: {}", window_text);
                break;
            }
            hwnd = FindWindowEx(NULL, hwnd, NULL, NULL);
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

    // 检查捕获间隔
    auto current_time = std::chrono::steady_clock::now();
    auto ms_since_last_capture = std::chrono::duration_cast<std::chrono::milliseconds>(
        current_time - std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(last_capture_time_))).count();

    if (ms_since_last_capture < minimum_capture_interval_ms_) {
        // 捕获太频繁，重用上一帧
        if (last_capture_result_) {
            return last_capture_result_;
        }
    }

    last_capture_time_ = std::chrono::steady_clock::now().time_since_epoch().count();

    // 更新窗口矩形
    GetWindowRect(game_window_, &window_rect_);

    // 计算窗口大小
    int width = window_rect_.right - window_rect_.left;
    int height = window_rect_.bottom - window_rect_.top;

    // 检查窗口是否过小或无效
    if (width <= 0 || height <= 0 || width > 7680 || height > 4320) {
        logError_fmt("无效的窗口尺寸: {}x{}", width, height);
        return nullptr;
    }

    std::vector<uint8_t> jpeg_data;

    // 使用DXGI或fallback到GDI+
    if (use_dxgi_ && dxgi_capture_) {
        jpeg_data = dxgi_capture_->captureScreen(window_rect_, quality);

        // 如果DXGI失败，fallback到GDI+
        if (jpeg_data.empty()) {
            use_dxgi_ = false;
            logWarn("DXGI捕获失败，切换到GDI+捕获");
        }
    }

    // 如果DXGI捕获失败或未使用DXGI，尝试GDI+捕获
    if (jpeg_data.empty()) {
        // 创建位图
        HBITMAP bitmap = NULL;
        if (!captureWindowImage(bitmap, width, height)) {
            logError("捕获窗口图像失败");
            return nullptr;
        }

        // 压缩为JPEG
        jpeg_data = compressToJpeg(bitmap, width, height, quality);

        // 清理位图
        cleanupBitmap(bitmap);
    }

    if (jpeg_data.empty()) {
        logError("JPEG压缩失败");
        return nullptr;
    }

    // 计算当前帧的哈希值用于帧差异检测
    size_t frame_hash = 0;
    if (jpeg_data.size() > 0) {
        // 简单哈希：只取JPEG的部分数据点计算
        for (size_t i = 0; i < jpeg_data.size(); i += 64) {
            frame_hash = frame_hash * 33 + jpeg_data[i];
        }
    }

    // 检查帧差异
    bool significant_change = (frame_hash != last_frame_hash_);
    last_frame_hash_ = frame_hash;

    if (!significant_change && last_capture_result_) {
        // 帧无明显变化，重用上一帧
        return last_capture_result_;
    }

    // 创建结果
    auto result = std::make_shared<CaptureResult>();
    result->jpeg_data = std::move(jpeg_data);
    result->width = width;
    result->height = height;
    result->window_rect = window_rect_;
    result->timestamp = std::chrono::system_clock::now();
    result->changed = significant_change;

    // 保存结果以便重用
    last_capture_result_ = result;

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

    // 尝试使用PrintWindow捕获
    BOOL result = PrintWindow(game_window_, memory_dc_, PW_CLIENTONLY);

    // 如果PrintWindow失败，尝试使用BitBlt
    if (!result || GetLastError() != 0) {
        result = BitBlt(memory_dc_, 0, 0, width, height, window_dc_, 0, 0, SRCCOPY);
    }

    // 恢复旧位图
    SelectObject(memory_dc_, old_bitmap);

    if (!result) {
        logError_fmt("捕获窗口内容失败，错误码: {}", GetLastError());
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
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &istream);
    if (FAILED(hr)) {
        logError_fmt("创建流失败: 0x{:X}", hr);
        delete bmp;
        return {};
    }

    // 获取JPEG编码器
    CLSID jpegClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) == -1) {
        logError("获取JPEG编码器失败");
        istream->Release();
        delete bmp;
        return {};
    }

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
    hr = istream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) {
        logError_fmt("获取流状态失败: 0x{:X}", hr);
        istream->Release();
        return {};
    }

    ULONG size = stat.cbSize.LowPart;

    // 分配内存
    std::vector<uint8_t> buffer(size);

    // 重置位置
    LARGE_INTEGER li = { 0 };
    istream->Seek(li, STREAM_SEEK_SET, NULL);

    // 读取数据
    ULONG bytesRead;
    hr = istream->Read(buffer.data(), size, &bytesRead);
    if (FAILED(hr) || bytesRead != size) {
        logError_fmt("读取流数据失败: 0x{:X}", hr);
        istream->Release();
        return {};
    }

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