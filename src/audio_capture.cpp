#include "audio_capture.h"

namespace invisible {

// -----------------------------------------------------------------------------
// COM Smart Pointer Helper
// -----------------------------------------------------------------------------

template<typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

// -----------------------------------------------------------------------------
// AudioCapture Implementation
// -----------------------------------------------------------------------------

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    Stop();
    
    if (captureEvent_) {
        CloseHandle(captureEvent_);
        captureEvent_ = nullptr;
    }
    
    if (mixFormat_) {
        CoTaskMemFree(mixFormat_);
        mixFormat_ = nullptr;
    }
    
    SafeRelease(captureClient_);
    SafeRelease(audioClient_);
    SafeRelease(device_);
    SafeRelease(deviceEnumerator_);
}

bool AudioCapture::Initialize(const AudioCaptureConfig& config) {
    if (initialized_) {
        LogError(L"AudioCapture already initialized");
        return false;
    }
    
    config_ = config;
    HRESULT hr;
    
    // Create device enumerator
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&deviceEnumerator_)
    );
    
    if (FAILED(hr)) {
        LogError(L"Failed to create MMDeviceEnumerator");
        return false;
    }
    
    // Get the default audio output device (we'll capture its output via loopback)
    // For loopback capture, we need the RENDER device, not the CAPTURE device
    if (config_.deviceId.empty()) {
        hr = deviceEnumerator_->GetDefaultAudioEndpoint(
            eRender,           // Data flow: render (output)
            eConsole,          // Role: console (default for games/apps)
            &device_
        );
    } else {
        hr = deviceEnumerator_->GetDevice(config_.deviceId.c_str(), &device_);
    }
    
    if (FAILED(hr)) {
        LogError(L"Failed to get audio device");
        return false;
    }
    
    // Get device name for logging
    IPropertyStore* props = nullptr;
    hr = device_->OpenPropertyStore(STGM_READ, &props);
    if (SUCCEEDED(hr)) {
        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
        if (SUCCEEDED(hr)) {
            std::wcout << L"[INFO] Using audio device: " << varName.pwszVal << std::endl;
            PropVariantClear(&varName);
        }
        props->Release();
    }
    
    // Activate the audio client
    hr = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&audioClient_)
    );
    
    if (FAILED(hr)) {
        LogError(L"Failed to activate audio client");
        return false;
    }
    
    // Get the mix format (native format of the audio engine)
    hr = audioClient_->GetMixFormat(&mixFormat_);
    if (FAILED(hr)) {
        LogError(L"Failed to get mix format");
        return false;
    }
    
    // Store format info
    format_.sampleRate = mixFormat_->nSamplesPerSec;
    format_.bitsPerSample = mixFormat_->wBitsPerSample;
    format_.channels = mixFormat_->nChannels;
    format_.blockAlign = mixFormat_->nBlockAlign;
    format_.avgBytesPerSec = mixFormat_->nAvgBytesPerSec;
    
    // Check if it's floating point
    if (mixFormat_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        format_.isFloat = true;
    } else if (mixFormat_->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat_);
        format_.isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    
    LogInfo((L"Audio format: " + format_.ToString()).c_str());
    
    // Create event for event-driven capture
    if (config_.useEventDriven) {
        captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!captureEvent_) {
            LogError(L"Failed to create capture event");
            return false;
        }
    }
    
    // Calculate buffer size
    REFERENCE_TIME bufferDuration = config_.bufferDurationMs * 10000;  // 100ns units
    
    // Initialize the audio client for LOOPBACK capture
    // AUDCLNT_STREAMFLAGS_LOOPBACK is the key flag that enables capturing
    // the audio output without needing a separate virtual audio device
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
    if (config_.useEventDriven) {
        streamFlags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    }
    
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        bufferDuration,
        0,              // Periodicity (0 for shared mode)
        mixFormat_,
        nullptr         // Audio session GUID
    );
    
    if (FAILED(hr)) {
        LogError(L"Failed to initialize audio client for loopback");
        return false;
    }
    
    // Set event handle for event-driven capture
    if (captureEvent_) {
        hr = audioClient_->SetEventHandle(captureEvent_);
        if (FAILED(hr)) {
            LogError(L"Failed to set event handle");
            return false;
        }
    }
    
    // Get the capture client interface
    hr = audioClient_->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&captureClient_)
    );
    
    if (FAILED(hr)) {
        LogError(L"Failed to get capture client");
        return false;
    }
    
    initialized_ = true;
    LogInfo(L"Audio capture initialized (WASAPI loopback mode)");
    return true;
}

bool AudioCapture::Start(IAudioCaptureHandler* handler) {
    if (!initialized_) {
        LogError(L"AudioCapture not initialized");
        return false;
    }
    
    if (capturing_) {
        return true;  // Already capturing
    }
    
    handler_ = handler;
    shouldStop_ = false;
    
    // Start the audio client
    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        LogError(L"Failed to start audio client");
        return false;
    }
    
    // Start capture thread
    captureThread_ = std::thread(&AudioCapture::CaptureThreadProc, this);
    capturing_ = true;
    
    LogInfo(L"Audio capture started");
    return true;
}

void AudioCapture::Stop() {
    if (!capturing_) return;
    
    shouldStop_ = true;
    
    // Signal the event to wake up the capture thread
    if (captureEvent_) {
        SetEvent(captureEvent_);
    }
    
    // Wait for capture thread
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    
    // Stop the audio client
    if (audioClient_) {
        audioClient_->Stop();
    }
    
    capturing_ = false;
    handler_ = nullptr;
    
    LogInfo(L"Audio capture stopped");
}

bool AudioCapture::IsCapturing() const {
    return capturing_;
}

void AudioCapture::CaptureThreadProc() {
    // Initialize COM for this thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        if (handler_) {
            handler_->OnCaptureError(hr, L"Failed to initialize COM on capture thread");
        }
        return;
    }
    
    while (!shouldStop_) {
        if (config_.useEventDriven) {
            // Wait for audio data or stop signal
            DWORD waitResult = WaitForSingleObject(captureEvent_, 100);
            
            if (waitResult == WAIT_OBJECT_0 && !shouldStop_) {
                ProcessAudioPacket();
            }
        } else {
            // Polling mode
            ProcessAudioPacket();
            Sleep(10);  // Small sleep to prevent CPU spinning
        }
    }
    
    CoUninitialize();
}

void AudioCapture::ProcessAudioPacket() {
    if (!captureClient_ || !handler_) return;
    
    BYTE* data = nullptr;
    UINT32 framesAvailable = 0;
    DWORD flags = 0;
    UINT64 devicePosition = 0;
    UINT64 qpcPosition = 0;
    
    HRESULT hr = captureClient_->GetBuffer(
        &data,
        &framesAvailable,
        &flags,
        &devicePosition,
        &qpcPosition
    );
    
    if (FAILED(hr)) {
        if (hr != AUDCLNT_S_BUFFER_EMPTY) {
            handler_->OnCaptureError(hr, L"Failed to get capture buffer");
        }
        return;
    }
    
    if (framesAvailable > 0) {
        // Calculate buffer size
        size_t bufferSize = framesAvailable * format_.blockAlign;
        
        // Check for silence flag
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            // The audio engine reports this as silence
            // We can either skip it or send zeros
            std::vector<BYTE> silenceBuffer(bufferSize, 0);
            AudioBuffer buffer(silenceBuffer.data(), bufferSize, framesAvailable, qpcPosition);
            handler_->OnAudioData(buffer, format_);
        } else {
            // Normal audio data
            AudioBuffer buffer(data, bufferSize, framesAvailable, qpcPosition);
            handler_->OnAudioData(buffer, format_);
        }
    }
    
    // Release the buffer
    hr = captureClient_->ReleaseBuffer(framesAvailable);
    if (FAILED(hr)) {
        handler_->OnCaptureError(hr, L"Failed to release capture buffer");
    }
}

std::vector<std::pair<std::wstring, std::wstring>> AudioCapture::EnumerateOutputDevices() {
    std::vector<std::pair<std::wstring, std::wstring>> devices;
    
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator)
    );
    
    if (FAILED(hr)) return devices;
    
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        collection->GetCount(&count);
        
        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = nullptr;
            hr = collection->Item(i, &device);
            
            if (SUCCEEDED(hr)) {
                LPWSTR id = nullptr;
                hr = device->GetId(&id);
                
                if (SUCCEEDED(hr)) {
                    IPropertyStore* props = nullptr;
                    hr = device->OpenPropertyStore(STGM_READ, &props);
                    
                    if (SUCCEEDED(hr)) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
                        
                        if (SUCCEEDED(hr)) {
                            devices.emplace_back(id, varName.pwszVal);
                            PropVariantClear(&varName);
                        }
                        props->Release();
                    }
                    CoTaskMemFree(id);
                }
                device->Release();
            }
        }
        collection->Release();
    }
    
    enumerator->Release();
    return devices;
}

// -----------------------------------------------------------------------------
// AudioBufferQueue Implementation
// -----------------------------------------------------------------------------

AudioBufferQueue::AudioBufferQueue(size_t maxBuffers)
    : maxBuffers_(maxBuffers) {}

AudioBufferQueue::~AudioBufferQueue() = default;

void AudioBufferQueue::OnAudioData(const AudioBuffer& buffer, const AudioFormat& format) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    format_ = format;
    
    // Drop old buffers if queue is full
    while (buffers_.size() >= maxBuffers_) {
        buffers_.pop();
    }
    
    buffers_.push(buffer);
    cv_.notify_one();
}

void AudioBufferQueue::OnCaptureError(HRESULT hr, const wchar_t* context) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastError_ = hr;
    LogError(context, hr);
}

bool AudioBufferQueue::PopBuffer(AudioBuffer& buffer, UINT32 timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (timeoutMs == INFINITE) {
        cv_.wait(lock, [this] { return !buffers_.empty(); });
    } else {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this] { return !buffers_.empty(); })) {
            return false;
        }
    }
    
    buffer = std::move(buffers_.front());
    buffers_.pop();
    return true;
}

bool AudioBufferQueue::HasBuffers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !buffers_.empty();
}

void AudioBufferQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<AudioBuffer> empty;
    std::swap(buffers_, empty);
}

AudioFormat AudioBufferQueue::GetFormat() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_;
}

} // namespace invisible
