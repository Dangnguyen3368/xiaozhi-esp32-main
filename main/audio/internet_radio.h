#pragma once

#include <array>
#include <atomic>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class AudioCodec;

class InternetRadio {
public:
    using VisualizerCallback = std::function<void(bool, const std::array<uint8_t, 8>&)>;

    explicit InternetRadio(AudioCodec* codec, VisualizerCallback visualizer_callback);
    bool Start(const char* station);
    void Stop();
    bool IsRunning() const;

private:
    static void TaskEntry(void* arg);
    void Run();
    void UpdateVisualizer(const int16_t* pcm, size_t samples);

    AudioCodec* codec_;
    VisualizerCallback visualizer_callback_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    TaskHandle_t task_handle_ = nullptr;
    TickType_t last_visualizer_tick_ = 0;
    const char* station_name_ = "RFI Tiếng Việt";
    const char* stream_url_ = nullptr;
};
