#pragma warning(disable: 4819)
#include <Windows.h>
#include <chrono>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avformat.lib")
#pragma warning(disable: 4996)


class FFFrameRecorder
{
    AVFrame* _videoFrame = nullptr;
    AVCodecContext* _cctx = nullptr;
    SwsContext* _swsCtx = nullptr;
    AVFormatContext* _ofctx = nullptr;
    const AVOutputFormat* _oformat = nullptr;
    int _fps = 30;
    int _srcWidth = 1920;
    int _srcHeight = 1080;
    int _bitrate = 2000;
    int _frameNo = 0;

public:
    void PushFrame(const uint8_t* data)
    {
        const int inLinesize[1] = { 4 * _srcWidth };
        //From RGB to YUV, and scale resolution
        sws_scale(_swsCtx, (const uint8_t* const*)&data, inLinesize, 0, _srcHeight, _videoFrame->data, _videoFrame->linesize);
        _videoFrame->pts = 90000 * _frameNo * 1 / 30;
        ++_frameNo;

        if (int err = avcodec_send_frame(_cctx, _videoFrame); err < 0)
        {
            return;
        }
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = nullptr;
        pkt.size = 0;
        pkt.flags |= AV_PKT_FLAG_KEY;
        if (avcodec_receive_packet(_cctx, &pkt) == 0)
        {
            av_interleaved_write_frame(_ofctx, &pkt);
            av_packet_unref(&pkt);
        }
    }


    int Init(const char* const fullName, int fps, AVPixelFormat pixFormat,  int srcWidth, int srcHeight, int dstWidth, int dstHeight)
    {
        _fps = fps;
        _bitrate = 1200;
        _frameNo = 0;
        _srcWidth = srcWidth;
        _srcHeight = srcHeight;

        av_log_set_flags(AV_LOG_QUIET);
        av_log_set_callback(LogCallBack);

        _oformat = av_guess_format(nullptr, fullName, nullptr);
        if (!_oformat)
        {
            return false;
        }
        //oformat->video_codec = AV_CODEC_ID_H265;

        if (int err = avformat_alloc_output_context2(&_ofctx, _oformat, nullptr, fullName); err)
        {
            return false;
        }

        const AVCodec* codec = avcodec_find_encoder(_oformat->video_codec);
        if (!codec)
        {
            return false;
        }

        AVStream* stream = avformat_new_stream(_ofctx, codec);
        if (!stream)
        {
            return false;
        }

        _cctx = avcodec_alloc_context3(codec);
        if (!_cctx)
        {
            return false;
        }

        stream->codecpar->codec_id = _oformat->video_codec;
        stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        stream->codecpar->width = dstWidth;
        stream->codecpar->height = dstHeight;
        stream->codecpar->format = AV_PIX_FMT_YUV420P;
        stream->codecpar->bit_rate = _bitrate * 1000;
        avcodec_parameters_to_context(_cctx, stream->codecpar);
        _cctx->time_base = AVRational{ 1, 1 };
        _cctx->max_b_frames = 2;
        _cctx->gop_size = 12;
        _cctx->framerate = AVRational{ _fps, 1 };
        if (stream->codecpar->codec_id == AV_CODEC_ID_H264 || stream->codecpar->codec_id == AV_CODEC_ID_H265)
        {
            av_opt_set(_cctx, "preset", "ultrafast", 0);
        }
        avcodec_parameters_from_context(stream->codecpar, _cctx);
        if (int err = avcodec_open2(_cctx, codec, nullptr); err < 0)
        {
            return false;
        }

        if (!(_oformat->flags & AVFMT_NOFILE))
        {
            if (int err = avio_open(&_ofctx->pb, fullName, AVIO_FLAG_WRITE); err < 0)
            {
                return false;
            }
        }

        if (int err = avformat_write_header(_ofctx, nullptr); err < 0)
        {
            return false;
        }

        av_dump_format(_ofctx, 0, fullName, 1);

        {
            _videoFrame = av_frame_alloc();
            _videoFrame->format = AV_PIX_FMT_YUV420P;
            _videoFrame->width = dstWidth;
            _videoFrame->height = dstHeight;
            if (int err = av_frame_get_buffer(_videoFrame, 0); err < 0)
            {
                return false;
            }
        }

        _swsCtx = sws_getContext(srcWidth, srcHeight, pixFormat, dstWidth, dstHeight, AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);

       
        return true;
    }

    void Release()
    {
        Finish();
        Free();
    }

    int SrcWidth() const
    {
        return _srcWidth;
    }

    int SrcHeight() const
    {
        return _srcHeight;
    }

private:

    static void LogCallBack(void*, int, const char*, va_list)
    {

    }

    void Finish()
    {
        //DELAYED FRAMES
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = nullptr;
        pkt.size = 0;

        for (;;)
        {
            avcodec_send_frame(_cctx, nullptr);
            if (avcodec_receive_packet(_cctx, &pkt) == 0)
            {
                av_interleaved_write_frame(_ofctx, &pkt);
                av_packet_unref(&pkt);
            }
            else {
                break;
            }
        }

        av_write_trailer(_ofctx);
        if (!(_oformat->flags & AVFMT_NOFILE)) {
            int err = avio_close(_ofctx->pb);
            if (err < 0)
            {
            }
        }
    }

    void Free()
    {
        if (_videoFrame)
        {
            av_frame_free(&_videoFrame);
        }
        if (_cctx)
        {
            avcodec_free_context(&_cctx);
        }
        if (_ofctx)
        {
            avformat_free_context(_ofctx);
            _ofctx = nullptr;
        }
        if (_swsCtx)
        {
            sws_freeContext(_swsCtx);
            _swsCtx = nullptr;
        }
    }
};




class WindowRecorder
{
public:
    static std::tuple<int, int> CalcPad(int srcWidth, int srcHeight, int videoWidth, int videoHeight)
    {
        const double srcAspect = (double)srcWidth / (double)srcHeight;
        const double dstAspect = (double)videoWidth / (double)videoHeight;
        const bool padWidth = srcAspect < dstAspect;
        const bool padHeight = srcAspect > dstAspect;
        int padW = 0;
        int padH = 0;
        if (padHeight)
        {
            padH = (int)(srcWidth / dstAspect) - srcHeight;
        }
        else if (padWidth)
        {
            padW = (int)(dstAspect * srcHeight) - srcWidth;
        }
        return std::make_tuple(padW, padH);
    }


    static HMONITOR GetPrimaryMonitor()
    {
        POINT ptZero = { 0, 0 };
        return MonitorFromPoint(ptZero,
            MONITOR_DEFAULTTOPRIMARY);
    }

    static float GetMonitorScalingRatio(HWND hwnd)
    {
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEX info = { };
        info.cbSize = sizeof(info);
        GetMonitorInfo(monitor, &info);
        DEVMODE devmode = {};
        devmode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings(info.szDevice, ENUM_CURRENT_SETTINGS, &devmode);
        return static_cast<float>(devmode.dmPelsWidth) / (info.rcMonitor.right - info.rcMonitor.left);
    }


    bool Init(const char* fileName, int fps, int videoWidth, int videoHeight)
    {
        if (_hTargetWnd != nullptr)
        {
            return false;
        }

#if false
        const HWND hUnityEditor = FindWindowW(TEXT("UnityContainerWndClass"), nullptr);
        if (hUnityEditor == nullptr)
        {
            return false;
        }
        _hTargetWnd = FindWindowExW(hUnityEditor, nullptr, TEXT("UnityGUIViewWndClass"), TEXT("UnityEditor.GameView"));
        if (_hTargetWnd == nullptr)
        {
            return false;
        }
#else
        _hTargetWnd = (HWND)0x000605C8;// FindWindowW(TEXT("TLoginForm"), nullptr);
#endif

        RECT rc;
        GetWindowRect(_hTargetWnd, &rc);

        const float monitorRatio = GetMonitorScalingRatio(_hTargetWnd);
        _srcWidth = (rc.right - rc.left);
        _srcHeight = (rc.bottom - rc.top);
        _srcWidth = int(_srcWidth * monitorRatio);
        _srcHeight = int(_srcHeight * monitorRatio);
        std::tie(_padW, _padH) = CalcPad(_srcWidth, _srcHeight, videoWidth, videoHeight);
        
        const int padWidth = _srcWidth + _padW;
        const int padHeight = _srcHeight + _padH;
        _recorder.Init(fileName, fps, AV_PIX_FMT_BGRA, padWidth, padHeight, videoWidth, videoHeight);
        _rawFrame = std::make_unique<uint8_t[]>(padWidth * padHeight * 4);

        return true;
    }

    void CapAndRecord()
    {
        HDC hDesktopDC = GetDC(_hTargetWnd);
        HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);
        HBITMAP hCaptureBitmap = CreateCompatibleBitmap(hDesktopDC, _recorder.SrcWidth(), _recorder.SrcHeight());
        SelectObject(hCaptureDC, hCaptureBitmap);

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = _recorder.SrcWidth();
        bmi.bmiHeader.biHeight = -_recorder.SrcHeight(); //upside down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        //BitBlt(hCaptureDC, 0, 0, _srcWidth, _srcHeight, hDesktopDC, 0, 0, SRCCOPY);
        BitBlt(hCaptureDC, _padW / 2, _padH / 2, _srcWidth, _srcHeight, hDesktopDC, 0, 0, SRCCOPY);
        //StretchBlt(hCaptureDC, _padW / 2, _padH / 2, _srcWidth, _srcHeight, hDesktopDC, 0, 0, _srcWidth, _srcHeight, SRCCOPY);
        GetDIBits(
            hCaptureDC,
            hCaptureBitmap,
            0,
            _recorder.SrcHeight(),
            _rawFrame.get(),
            &bmi,
            DIB_RGB_COLORS
        );
        _recorder.PushFrame(_rawFrame.get());

        ReleaseDC(_hTargetWnd, hDesktopDC);
        DeleteDC(hCaptureDC);
        DeleteObject(hCaptureBitmap);
    }


    void Release()
    {
        _recorder.Release();
        _hTargetWnd = nullptr;
        _rawFrame = nullptr;
    }
private:
    FFFrameRecorder _recorder;
    std::unique_ptr<uint8_t[]> _rawFrame;
    HWND _hTargetWnd = nullptr;
    int _padW = 0;
    int _padH = 0;
    int _srcWidth = 0;
    int _srcHeight = 0;
};

#if !defined(_CONSOLE)
extern "C"
{
    __declspec(dllexport)
        void* CreateInit(const char* name, int fps, int width, int height)
    {
        auto* r = new WindowRecorder();
        if (!r->Init(name, fps, width, height))
        {
            delete r;
            return nullptr;
        }
        
        return r;
    }

    __declspec(dllexport)
        void RecordFrame(void *inst)
    {
        
        static_cast<WindowRecorder*>(inst)->CapAndRecord();
    }

    __declspec(dllexport)
        void Stop(void* inst)
    {
        if (inst == nullptr)
        {
            return;
        }
        auto r = static_cast<WindowRecorder*>(inst);
        r->Release();
        delete r;
    }
}
#else

int main(int argc, char* argv[])
{
    WindowRecorder r;
    r.Init("test.mp4", 2, 1920, 1080);
    const auto frameTime = std::chrono::milliseconds(33);
    for (int frameID = 0; frameID < 128; ++frameID)
    {
        r.CapAndRecord();
    }
    r.Release();
    return 0;
}
#endif