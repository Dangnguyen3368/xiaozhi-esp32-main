#ifndef STUDY_UI_H
#define STUDY_UI_H

#include "study/physics_tutor.h"
#include "study/study_timer.h"

#include <lvgl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Touch screens for the study features, drawn as overlays on lv_layer_top. The methods take
// the LVGL lock themselves, so call them from any task except the LVGL task (button handlers
// run in the LVGL task and therefore only schedule their action on the main task).

// Big-button menu opened by a long press on the screen.
class StudyMenu {
public:
    struct Actions {
        std::function<void()> practice;
        std::function<void()> review;
        std::function<void()> toggle_pomodoro;
        std::function<void(const std::string& topic)> formulas;
        std::function<void()> progress;
        std::function<void()> toggle_radio;
        std::function<void()> local_music;  // play the computer's music, or skip when already playing
        std::function<void()> update_firmware;
        std::function<void()> wifi_setup;
        // Computer control: run an action (type, key, open, pair) in the background.
        std::function<void(const std::string& action, const std::string& text)> pc_command;
        std::function<bool()> pc_paired;
        std::function<void()> start_dictation;  // start listening so the user can dictate
        std::function<std::string()> phone_photo_url;  // address the phone uploads exam photos to
        // Labels reflect what the toggles will do.
        std::function<bool()> pomodoro_running;
        std::function<bool()> radio_running;
        std::function<bool()> local_music_running;
    };

    explicit StudyMenu(Actions actions) : actions_(std::move(actions)) {}
    void Show();
    void ShowFormulaTopics();
    void ShowComputer();
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    enum class Page { kMain, kFormulas, kComputer, kPair, kPhoto };
    void Build(Page page);
    void BuildComputer(lv_obj_t* grid, const std::function<void(const char*, lv_color_t, std::function<void()>)>& add);
    void BuildPair(const std::function<void(const char*, lv_color_t, std::function<void()>)>& add);

    Actions actions_;
    std::string pair_code_;
    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::atomic<bool> shown_{false};
};

// Bar chart of the active learner's progress per topic.
class ProgressView {
public:
    void Show(const PhysicsTutor::ProgressReport& report);
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::atomic<bool> shown_{false};
};

// Always-visible "Menu" button in the bottom-right corner; a tap opens the study menu.
class MenuButton {
public:
    static constexpr int kWidth = 64;
    static constexpr int kHeight = 30;
    static constexpr int kMargin = 4;
    // Call with the LVGL lock held, after the display exists.
    void Create(std::function<void()> on_click);
    // True when a touch at (x, y) lands on the button (with a little slack).
    static bool Contains(int x, int y, int screen_width, int screen_height);

private:
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
};

// Small countdown pill for the Pomodoro timer, kept beneath any full-screen overlay.
class PomodoroBadge {
public:
    void Update(const StudyTimer::State& state);

private:
    lv_obj_t* label_ = nullptr;
};

namespace study_ui {
// Shared styling and wiring for the overlays above.
lv_obj_t* CreateOverlay();
lv_obj_t* AddLabel(lv_obj_t* parent, const std::string& text, lv_color_t color);
// Adds a button whose click runs `action` on the main task. The handler object is kept in
// `handlers`, which must outlive the button.
lv_obj_t* AddButton(lv_obj_t* parent, const std::string& text, lv_color_t color, std::function<void()> action,
                    std::vector<std::unique_ptr<std::function<void()>>>& handlers);
// Makes a click on `obj` itself (not on a child button) run `action` on the main task.
void OnClick(lv_obj_t* obj, std::function<void()> action,
             std::vector<std::unique_ptr<std::function<void()>>>& handlers);
const lv_font_t* TextFont();
}  // namespace study_ui

#endif  // STUDY_UI_H
