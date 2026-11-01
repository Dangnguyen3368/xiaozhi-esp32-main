#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class AudioCodec;

class InternetRadio {
public:
    using VisualizerCallback = std::function<void(bool, const std::array<uint8_t, 8>&)>;

    struct Station {
        const char* id;    // what the assistant passes as `station`
        const char* name;  // shown on screen
        const char* url;   // direct MP3 stream (HTTP or HTTPS, no playlist); nullptr = local
    };

    // Station id of the music server on the user's computer (tools/music_server/robot_may_tinh.py).
    static constexpr const char* kLocalStation = "local";

    // Returns nullptr when `id` is not a known station.
    static const Station* FindStation(const std::string& id);
    // "chill (Groove Salad), ambient (...)" for tool descriptions and errors.
    static std::string StationList();

    explicit InternetRadio(AudioCodec* codec, VisualizerCallback visualizer_callback);
    bool Start(const char* station);
    void Stop();
    bool IsRunning() const;
    bool IsLocal() const { return running_.load() && current_ != nullptr && current_->url == nullptr; }
    // Asks the computer's music server to skip to the next song. Returns false when not playing
    // local music or the computer did not answer.
    bool NextTrack();
    const char* station_name() const { return station_name_; }

private:
    static void TaskEntry(void* arg);
    void Run();
    void UpdateVisualizer(const int16_t* pcm, size_t samples);
    // Listens for the computer's UDP announcement; falls back to the last address that worked.
    bool FindLocalServer();

    AudioCodec* codec_;
    VisualizerCallback visualizer_callback_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    TaskHandle_t task_handle_ = nullptr;
    TickType_t last_visualizer_tick_ = 0;
    const char* station_name_ = "";
    const char* stream_url_ = nullptr;
    const Station* current_ = nullptr;
    std::string local_base_url_;  // "http://192.168.1.5:8765"
    std::string local_stream_url_;
};
