#include "screen_capture.h"
#include "spdlog/spdlog.h"
#include <gdiplus.h>
#include <memory>
#include <algorithm>
#include <objidl.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
namespace fmt {
    template<>
    struct formatter<Gdiplus::Status> : formatter<int> {
        auto format(const Gdiplus::Status& status, format_context& ctx) const {
            return formatter<int>::format(static_cast<int>(status), ctx);
        }
    };
}

// RAII������GDI+
class GdiplusInitializer {
public:
    GdiplusInitializer() {
        GdiplusStartupInput gdiplusStartupInput;
        Status status = GdiplusStartup(&token_, &gdiplusStartupInput, NULL);
        if (status != Ok) {
            spdlog::error("GDI+��ʼ��ʧ��: {}", status);
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

// ��̬GDI+��ʼ����
static GdiplusInitializer gdiplusInit;

// ������������ȡJPEG������CLSID
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;          // ����������
    UINT size = 0;         // �����������С

    // ��ȡͼ���������Ϣ
    GetImageEncodersSize(&num, &size);
    if (size == 0) {
        return -1;
    }

    // �����ڴ�
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) {
        return -1;
    }

    // ��ȡ��������Ϣ
    GetImageEncoders(num, size, pImageCodecInfo);

    // ����ָ����ʽ�ı�����
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
        spdlog::error("GDI+��ʼ��ʧ�ܣ���Ļ���񽫲�����");
    }
    gdiplusToken_ = gdiplusInit.getToken();
}

ScreenCapture::~ScreenCapture() {
    // ������Դ
    if (memory_dc_) {
        DeleteDC(memory_dc_);
    }
    if (window_dc_) {
        ReleaseDC(game_window_, window_dc_);
    }
}

bool ScreenCapture::initialize(const std::string& window_title) {
    window_title_ = window_title;

    // ������Ϸ����
    if (!findGameWindow()) {
        spdlog::error("�Ҳ�����Ϸ����: {}", window_title);
        return false;
    }

    // ��ȡ����DC
    window_dc_ = GetDC(game_window_);
    if (!window_dc_) {
        spdlog::error("��ȡ����DCʧ��");
        return false;
    }

    // �����ڴ�DC
    memory_dc_ = CreateCompatibleDC(window_dc_);
    if (!memory_dc_) {
        spdlog::error("�����ڴ�DCʧ��");
        ReleaseDC(game_window_, window_dc_);
        window_dc_ = NULL;
        return false;
    }

    spdlog::info("��Ļ�����ʼ���ɹ���Ŀ�괰��: {}", window_title);
    return true;
}

bool ScreenCapture::findGameWindow() {
    // ������Ϸ����
    game_window_ = FindWindowA(NULL, window_title_.c_str());
    if (!game_window_) {
        // ����ʹ�ò��ֱ���ƥ��
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

    // ��ȡ���ھ���
    GetWindowRect(game_window_, &window_rect_);

    // ��鴰���Ƿ�ɼ�
    if (!IsWindowVisible(game_window_)) {
        spdlog::error("��Ϸ���ڲ��ɼ�");
        game_window_ = NULL;
        return false;
    }

    spdlog::info("�ҵ���Ϸ���ڣ����: {:x}, ��С: {}x{}",
        reinterpret_cast<uintptr_t>(game_window_),
        window_rect_.right - window_rect_.left,
        window_rect_.bottom - window_rect_.top);

    return true;
}

std::shared_ptr<CaptureResult> ScreenCapture::captureScreen(int quality) {
    // ��鴰���Ƿ���Ч
    if (!isWindowValid()) {
        spdlog::error("��Ч����Ϸ����");
        return nullptr;
    }

    // ���´��ھ���
    GetWindowRect(game_window_, &window_rect_);

    // ���㴰�ڴ�С
    int width = window_rect_.right - window_rect_.left;
    int height = window_rect_.bottom - window_rect_.top;

    // ����λͼ
    HBITMAP bitmap = NULL;
    if (!captureWindowImage(bitmap, width, height)) {
        spdlog::error("���񴰿�ͼ��ʧ��");
        return nullptr;
    }

    // ѹ��ΪJPEG
    std::vector<uint8_t> jpeg_data = compressToJpeg(bitmap, width, height, quality);

    // ����λͼ
    cleanupBitmap(bitmap);

    if (jpeg_data.empty()) {
        spdlog::error("JPEGѹ��ʧ��");
        return nullptr;
    }

    // �������
    auto result = std::make_shared<CaptureResult>();
    result->jpeg_data = std::move(jpeg_data);
    result->width = width;
    result->height = height;
    result->window_rect = window_rect_;
    result->timestamp = std::chrono::system_clock::now();

    return result;
}

bool ScreenCapture::captureWindowImage(HBITMAP& bitmap, int& width, int& height) {
    // ��鴰���Ƿ���С��
    if (IsIconic(game_window_)) {
        spdlog::warn("��Ϸ��������С�����޷�����");
        return false;
    }

    // ��������λͼ
    bitmap = CreateCompatibleBitmap(window_dc_, width, height);
    if (!bitmap) {
        spdlog::error("��������λͼʧ��");
        return false;
    }

    // ѡ��λͼ���ڴ�DC
    HBITMAP old_bitmap = (HBITMAP)SelectObject(memory_dc_, bitmap);

    // ���ƴ������ݵ�λͼ
    BOOL result = PrintWindow(game_window_, memory_dc_, PW_CLIENTONLY);

    if (!result) {
        // ���PrintWindowʧ�ܣ�����ʹ��BitBlt
        result = BitBlt(memory_dc_, 0, 0, width, height, window_dc_, 0, 0, SRCCOPY);
    }

    // �ָ���λͼ
    SelectObject(memory_dc_, old_bitmap);

    if (!result) {
        spdlog::error("���񴰿�����ʧ��");
        DeleteObject(bitmap);
        bitmap = NULL;
        return false;
    }

    return true;
}

std::vector<uint8_t> ScreenCapture::compressToJpeg(HBITMAP bitmap, int width, int height, int quality) {
    // ����Bitmap����
    Bitmap* bmp = new Bitmap(bitmap, NULL);
    if (!bmp) {
        spdlog::error("����GDI+ Bitmapʧ��");
        return {};
    }

    // ���λͼ�Ƿ���Ч
    if (bmp->GetLastStatus() != Ok) {
        spdlog::error("GDI+ Bitmap��Ч");
        delete bmp;
        return {};
    }

    // �����ڴ���
    IStream* istream = nullptr;
    CreateStreamOnHGlobal(NULL, TRUE, &istream);

    // ����JPEG������
    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);

    // ���ñ������
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG qualityValue = quality;
    encoderParams.Parameter[0].Value = &qualityValue;

    // ����ΪJPEG
    Status status = bmp->Save(istream, &jpegClsid, &encoderParams);

    // ����λͼ
    delete bmp;

    if (status != Ok) {
        spdlog::error("����JPEGʧ��: {}", status);
        istream->Release();
        return {};
    }

    // ��ȡ���ݴ�С
    STATSTG stat;
    istream->Stat(&stat, STATFLAG_NONAME);
    ULONG size = stat.cbSize.LowPart;

    // �����ڴ�
    std::vector<uint8_t> buffer(size);

    // ������λ��
    LARGE_INTEGER li = { 0 };
    istream->Seek(li, STREAM_SEEK_SET, NULL);

    // ��ȡ����
    ULONG bytesRead;
    istream->Read(buffer.data(), size, &bytesRead);

    // �ͷ���
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