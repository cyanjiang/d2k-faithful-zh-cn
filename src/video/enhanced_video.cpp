#include "video/enhanced_video.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#endif

namespace d2k::video {

std::filesystem::path enhanced_path_for(const std::filesystem::path& dir,
                                        const std::filesystem::path& vqa) {
    return dir / (vqa.stem().string() + ".mp4");
}

#ifdef _WIN32

namespace {

template <class T> void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Frames decoded ahead: ~6 MB each at 2560x1600, about 0.1 s at 60 fps.
constexpr size_t kAhead = 6;

class MfVideo final : public EnhancedVideo {
public:
    explicit MfVideo(std::filesystem::path file) : file_(std::move(file)) {}

    ~MfVideo() override {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    // Starts the worker and waits until it knows the frame size (or failed).
    bool start() {
        worker_ = std::thread([this] { run(); });
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return ready_ || failed_; });
        return ready_;
    }

    int width() const override { return width_; }
    int height() const override { return height_; }

    std::optional<EnhancedFrame> take_due(double seconds) override {
        std::optional<EnhancedFrame> due;
        {
            std::lock_guard lock(mutex_);
            while (!queue_.empty() && queue_.front().seconds <= seconds) {
                due = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (due) cv_.notify_all();
        return due;
    }

private:
    void fail(const char* what, HRESULT hr) {
        SDL_Log("enhanced movie %s: %s failed (0x%08lx); playing the original pictures",
                file_.string().c_str(), what, static_cast<unsigned long>(hr));
        std::lock_guard lock(mutex_);
        failed_ = true;
        cv_.notify_all();
    }

    void run() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            fail("MFStartup", hr);
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }
        decode();
        MFShutdown();
        if (SUCCEEDED(co)) CoUninitialize();
    }

    void decode() {
        IMFAttributes* attr = nullptr;
        IMFSourceReader* reader = nullptr;
        IMFMediaType* type = nullptr;
        IMFMediaType* current = nullptr;
        HRESULT hr = MFCreateAttributes(&attr, 1);
        if (SUCCEEDED(hr)) hr = attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(file_.wstring().c_str(), attr, &reader);
        if (FAILED(hr)) {
            release(attr);
            return fail("open", hr);
        }
        reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
        hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr))
            hr = reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
        if (SUCCEEDED(hr))
            hr = reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current);
        UINT32 w = 0, h = 0;
        if (SUCCEEDED(hr)) hr = MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
        release(type);
        release(current);
        release(attr);
        if (FAILED(hr) || w == 0 || h == 0 || (w & 1) || (h & 1)) {
            release(reader);
            return fail("NV12 output", hr);
        }
        {
            std::lock_guard lock(mutex_);
            width_ = int(w);
            height_ = int(h);
            ready_ = true;
        }
        cv_.notify_all();

        const size_t y_size = size_t(w) * h;
        while (true) {
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || queue_.size() < kAhead; });
                if (stop_) break;
            }
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
                                    &ts, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                release(sample);
                break;
            }
            if (!sample) continue; // a stream tick or a format note
            IMFMediaBuffer* buffer = nullptr;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
                release(sample);
                continue;
            }
            EnhancedFrame frame;
            frame.seconds = double(ts) / 1e7;
            frame.nv12.resize(y_size + y_size / 2);
            BYTE* src = nullptr;
            LONG pitch = 0;
            IMF2DBuffer* buffer2d = nullptr;
            bool copied = false;
            if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))) &&
                SUCCEEDED(buffer2d->Lock2D(&src, &pitch))) {
                // NV12 in a 2D buffer: the UV plane follows the Y plane at
                // pitch * (height rounded up to the decoder's alignment);
                // the buffer length tells how many rows the Y plane has.
                DWORD length = 0;
                buffer->GetCurrentLength(&length);
                const size_t rows = pitch > 0 ? size_t(length) / size_t(pitch) * 2 / 3 : h;
                copy_nv12(frame.nv12.data(), src, size_t(pitch), w, h, rows < h ? h : rows);
                buffer2d->Unlock2D();
                copied = true;
            }
            release(buffer2d);
            if (!copied) {
                DWORD max_len = 0, len = 0;
                if (SUCCEEDED(buffer->Lock(&src, &max_len, &len))) {
                    if (len >= y_size + y_size / 2) copy_nv12(frame.nv12.data(), src, w, w, h, h);
                    buffer->Unlock();
                    copied = len >= y_size + y_size / 2;
                }
            }
            release(buffer);
            release(sample);
            if (!copied) continue;
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(frame));
        }
        release(reader);
    }

    static void copy_nv12(uint8_t* dst, const BYTE* src, size_t pitch, UINT32 w, UINT32 h,
                          size_t y_rows) {
        for (UINT32 y = 0; y < h; ++y) std::memcpy(dst + size_t(y) * w, src + y * pitch, w);
        const BYTE* uv = src + y_rows * pitch;
        uint8_t* duv = dst + size_t(w) * h;
        for (UINT32 y = 0; y < h / 2; ++y) std::memcpy(duv + size_t(y) * w, uv + y * pitch, w);
    }

    std::filesystem::path file_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<EnhancedFrame> queue_;
    bool stop_ = false, ready_ = false, failed_ = false;
    int width_ = 0, height_ = 0;
};

} // namespace

std::unique_ptr<EnhancedVideo> EnhancedVideo::open(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return nullptr;
    auto video = std::make_unique<MfVideo>(file);
    if (!video->start()) return nullptr;
    return video;
}

#else

std::unique_ptr<EnhancedVideo> EnhancedVideo::open(const std::filesystem::path& file) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(file, ec))
        SDL_Log("enhanced movie %s: no decoder on this system; playing the original pictures",
                file.string().c_str());
    return nullptr;
}

#endif

} // namespace d2k::video
