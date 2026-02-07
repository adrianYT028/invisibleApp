#pragma once

#include "utils.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "ole32.lib")

namespace invisible {

// -----------------------------------------------------------------------------
// Audio Format Information
// -----------------------------------------------------------------------------

struct AudioFormat {
    UINT32 sampleRate = 0;
    UINT16 bitsPerSample = 0;
    UINT16 channels = 0;
    UINT32 blockAlign = 0;
    UINT32 avgBytesPerSec = 0;
    bool isFloat = false;
    
    std::wstring ToString() const {
        std::wstringstream ss;
        ss << sampleRate << L" Hz, " << bitsPerSample << L"-bit, " 
           << channels << L" ch" << (isFloat ? L" (float)" : L"");
        return ss.str();
    }
};

// -----------------------------------------------------------------------------
// Audio Buffer
// -----------------------------------------------------------------------------

struct AudioBuffer {
    std::vector<BYTE> data;
    UINT64 timestamp = 0;  // QPC timestamp
    UINT32 frames = 0;
    
    AudioBuffer() = default;
    AudioBuffer(const BYTE* src, size_t size, UINT32 frameCount, UINT64 ts)
        : data(src, src + size), timestamp(ts), frames(frameCount) {}
};

// -----------------------------------------------------------------------------
// Audio Capture Callback Interface
// -----------------------------------------------------------------------------

class IAudioCaptureHandler {
public:
    virtual ~IAudioCaptureHandler() = default;
    virtual void OnAudioData(const AudioBuffer& buffer, const AudioFormat& format) = 0;
    virtual void OnCaptureError(HRESULT hr, const wchar_t* context) = 0;
};

// -----------------------------------------------------------------------------
// Audio Capture Configuration
// -----------------------------------------------------------------------------

struct AudioCaptureConfig {
    // Buffer duration in milliseconds
    UINT32 bufferDurationMs = 100;
    
    // Use event-driven capture (recommended)
    bool useEventDriven = true;
    
    // Target specific device (empty = default render device for loopback)
    std::wstring deviceId;
};

// -----------------------------------------------------------------------------
// WASAPI Loopback Audio Capture
// -----------------------------------------------------------------------------

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    
    // Disable copy
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;
    
    // Initialize capture (must be called from a COM-initialized thread)
    bool Initialize(const AudioCaptureConfig& config = AudioCaptureConfig());
    
    // Start capturing
    bool Start(IAudioCaptureHandler* handler);
    
    // Stop capturing
    void Stop();
    
    // Check if currently capturing
    bool IsCapturing() const;
    
    // Get the audio format
    AudioFormat GetFormat() const { return format_; }
    
    // Get available audio output devices
    static std::vector<std::pair<std::wstring, std::wstring>> EnumerateOutputDevices();
    
private:
    // Capture thread function
    void CaptureThreadProc();
    
    // Process captured audio
    void ProcessAudioPacket();
    
    // COM interfaces (must be released in order)
    IMMDeviceEnumerator* deviceEnumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audioClient_ = nullptr;
    IAudioCaptureClient* captureClient_ = nullptr;
    
    // Event for event-driven capture
    HANDLE captureEvent_ = nullptr;
    
    // Capture thread
    std::thread captureThread_;
    std::atomic<bool> shouldStop_{false};
    
    // Audio format
    AudioFormat format_;
    WAVEFORMATEX* mixFormat_ = nullptr;
    
    // Handler
    IAudioCaptureHandler* handler_ = nullptr;
    
    // Configuration
    AudioCaptureConfig config_;
    
    // State
    bool initialized_ = false;
    bool capturing_ = false;
};

// -----------------------------------------------------------------------------
// Simple Audio Buffer Queue (for async processing)
// -----------------------------------------------------------------------------

class AudioBufferQueue : public IAudioCaptureHandler {
public:
    explicit AudioBufferQueue(size_t maxBuffers = 100);
    ~AudioBufferQueue() override;
    
    // IAudioCaptureHandler implementation
    void OnAudioData(const AudioBuffer& buffer, const AudioFormat& format) override;
    void OnCaptureError(HRESULT hr, const wchar_t* context) override;
    
    // Pop a buffer (blocks if empty)
    bool PopBuffer(AudioBuffer& buffer, UINT32 timeoutMs = INFINITE);
    
    // Check if there are buffers available
    bool HasBuffers() const;
    
    // Clear all buffered data
    void Clear();
    
    // Get the current audio format
    AudioFormat GetFormat() const;
    
    // Get last error (if any)
    HRESULT GetLastError() const { return lastError_; }
    
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<AudioBuffer> buffers_;
    AudioFormat format_;
    size_t maxBuffers_;
    HRESULT lastError_ = S_OK;
};

} // namespace invisible
