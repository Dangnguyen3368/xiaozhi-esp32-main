#ifndef STUDY_TIMER_H
#define STUDY_TIMER_H

#include <esp_timer.h>

#include <functional>
#include <mutex>

// Pomodoro study timer: work for a while, rest a few minutes, repeat until stopped. At each
// switch the robot plays a tone and asks the assistant to tell the student. Optional music
// plays during work periods.
class StudyTimer {
public:
    struct State {
        bool running = false;
        bool on_break = false;
        int remaining_seconds = 0;
        int sessions_done = 0;  // finished work periods since Start()
        int work_minutes = 25;
        int break_minutes = 5;
        bool music = false;
    };

    static StudyTimer& GetInstance();

    // Registers the self.study.pomodoro MCP tool. Call once from the board's InitializeTools().
    void RegisterTool();

    bool Start(int work_minutes, int break_minutes, bool music);
    void Stop();
    State GetState();

    // Called in the main task every second while running, and once more after stopping.
    void SetListener(std::function<void(const State&)> listener) { listener_ = std::move(listener); }
    // Boards with a radio start (true) or stop (false) the background music through this.
    void SetMusicControl(std::function<void(bool)> control) { music_control_ = std::move(control); }

private:
    StudyTimer() = default;
    void Tick();
    void Notify();
    void SwitchPhase();

    std::mutex mutex_;
    State state_;
    int64_t phase_end_us_ = 0;
    esp_timer_handle_t timer_ = nullptr;
    std::function<void(const State&)> listener_;
    std::function<void(bool)> music_control_;
};

#endif  // STUDY_TIMER_H
