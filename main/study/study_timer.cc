#include "study_timer.h"

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"

#include <cJSON.h>
#include <esp_log.h>

#include <algorithm>
#include <string>

#define TAG "StudyTimer"

StudyTimer& StudyTimer::GetInstance() {
    static StudyTimer instance;
    return instance;
}

bool StudyTimer::Start(int work_minutes, int break_minutes, bool music) {
    if (timer_ == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = [](void* arg) {
            // Runs in the esp_timer task: do the work (sound, UI, assistant) in the main task.
            Application::GetInstance().Schedule([arg]() { static_cast<StudyTimer*>(arg)->Tick(); });
        };
        args.arg = this;
        args.name = "study_timer";
        if (esp_timer_create(&args, &timer_) != ESP_OK) {
            timer_ = nullptr;
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State{};
        state_.running = true;
        state_.work_minutes = std::clamp(work_minutes, 1, 120);
        state_.break_minutes = std::clamp(break_minutes, 1, 60);
        state_.music = music;
        state_.remaining_seconds = state_.work_minutes * 60;
        phase_end_us_ = esp_timer_get_time() + static_cast<int64_t>(state_.remaining_seconds) * 1000000;
    }
    esp_timer_stop(timer_);
    esp_timer_start_periodic(timer_, 1000 * 1000);
    ESP_LOGI(TAG, "Started: %d min work, %d min break%s", state_.work_minutes, state_.break_minutes,
             music ? ", music" : "");
    if (music && music_control_) {
        music_control_(true);
    }
    Notify();
    return true;
}

void StudyTimer::Stop() {
    bool was_music = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_.running) {
            return;
        }
        was_music = state_.music && !state_.on_break;
        state_.running = false;
        state_.remaining_seconds = 0;
    }
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
    }
    if (was_music && music_control_) {
        music_control_(false);
    }
    ESP_LOGI(TAG, "Stopped");
    Notify();
}

StudyTimer::State StudyTimer::GetState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void StudyTimer::Notify() {
    if (listener_) {
        listener_(GetState());
    }
}

void StudyTimer::Tick() {
    bool switch_phase = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_.running) {
            return;
        }
        const int64_t left_us = phase_end_us_ - esp_timer_get_time();
        state_.remaining_seconds = static_cast<int>(std::max<int64_t>(0, (left_us + 999999) / 1000000));
        switch_phase = state_.remaining_seconds == 0;
    }
    if (switch_phase) {
        SwitchPhase();
    }
    Notify();
}

void StudyTimer::SwitchPhase() {
    State state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.on_break = !state_.on_break;
        if (state_.on_break) {
            state_.sessions_done++;
        }
        const int minutes = state_.on_break ? state_.break_minutes : state_.work_minutes;
        state_.remaining_seconds = minutes * 60;
        phase_end_us_ = esp_timer_get_time() + static_cast<int64_t>(minutes) * 60 * 1000000;
        state = state_;
    }

    auto& app = Application::GetInstance();
    auto* display = Board::GetInstance().GetDisplay();
    std::string ask;
    if (state.on_break) {
        if (state.music && music_control_) {
            music_control_(false);
        }
        if (display != nullptr) {
            display->ShowNotification("Hết giờ học! Nghỉ " + std::to_string(state.break_minutes) + " phút", 10000);
        }
        ask = "Hết " + std::to_string(state.work_minutes) + " phút học rồi, em đã xong " +
              std::to_string(state.sessions_done) + " phiên. Hãy khen em thật ngắn và nhắc em nghỉ " +
              std::to_string(state.break_minutes) + " phút.";
    } else {
        if (display != nullptr) {
            display->ShowNotification("Hết giờ nghỉ! Học tiếp nào", 10000);
        }
        ask = "Hết giờ nghỉ rồi. Hãy nhắc em thật ngắn quay lại học " + std::to_string(state.work_minutes) +
              " phút nữa.";
    }
    ESP_LOGI(TAG, "Switching to %s", state.on_break ? "break" : "work");
    app.PlaySound(Lang::Sounds::OGG_POPUP);
    app.AskAssistant(ask);
    if (!state.on_break && state.music && music_control_) {
        // The radio waits for the assistant's reply to finish before it starts playing.
        music_control_(true);
    }
}

void StudyTimer::RegisterTool() {
    McpServer::GetInstance().AddTool(
        "self.study.pomodoro",
        "Pomodoro study timer (hẹn giờ học): the student works for `work_minutes`, rests for "
        "`break_minutes`, and it repeats until stopped. The robot rings and tells the student at "
        "each switch, and colour screens show a countdown. Set `music` true to play calm "
        "background music during work periods. Actions: `start`, `stop`, `status`.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("work_minutes", kPropertyTypeInteger, 25, 1, 120),
                      Property("break_minutes", kPropertyTypeInteger, 5, 1, 60),
                      Property("music", kPropertyTypeBoolean, false)}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto action = properties["action"].value<std::string>();
            if (action == "start") {
                if (!Start(properties["work_minutes"].value<int>(), properties["break_minutes"].value<int>(),
                           properties["music"].value<bool>())) {
                    cJSON* json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "error", "Could not start the timer.");
                    return json;
                }
            } else if (action == "stop") {
                Stop();
            } else if (action != "status") {
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "error", "Use action start, stop or status.");
                return json;
            }
            const auto state = GetState();
            cJSON* json = cJSON_CreateObject();
            cJSON_AddBoolToObject(json, "running", state.running);
            if (state.running) {
                cJSON_AddStringToObject(json, "phase", state.on_break ? "break" : "work");
                cJSON_AddNumberToObject(json, "remaining_seconds", state.remaining_seconds);
                cJSON_AddNumberToObject(json, "work_minutes", state.work_minutes);
                cJSON_AddNumberToObject(json, "break_minutes", state.break_minutes);
            }
            cJSON_AddNumberToObject(json, "sessions_done", state.sessions_done);
            return json;
        });
}
