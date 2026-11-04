#ifndef HOME_SCREEN_H
#define HOME_SCREEN_H

#include "app_launcher.h"

#include <lvgl.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Idle home screen for the 320x240 TFT: a big LED-style clock with the date, lunar date and
// weather (page 0), and the month calendar with lunar days (page 1). Shown once the robot has
// been idle for a while, hidden as soon as anything happens (chat, notification, music,
// another screen). Between 22:00 and 6:00 it dims the backlight and turns amber; a tap lights
// it up again for 10 seconds.
//
// Everything runs in the LVGL task: Start() creates a 1-second LVGL timer, and the touch
// callbacks that call HandleTap()/Dismiss()/ShowPage() also run there.
class HomeScreen {
public:
    struct Hooks {
        std::function<bool()> overlay_shown;           // a menu or study screen is open
        // Called with true while the home screen shows (its dock replaces the floating Menu
        // and GPS buttons), false when it hides.
        std::function<void(bool home_shown)> floating_buttons;
        std::vector<AppEntry> dock;  // up to 4 apps along the bottom of the clock page
    };

    // Call with the LVGL lock held.
    void Start(Hooks hooks);
    bool IsShown() const { return shown_; }
    // A tap on the home screen. Returns true when it only woke the dimmed night screen.
    bool HandleTap();
    // Hides the home screen until the robot has been idle again for a while.
    void Dismiss();
    // Swipe left/right: next or previous page.
    void ChangePage(int delta);
    // Shows the home screen now on `page` (0 clock, 1 calendar), e.g. from the app grid.
    void ShowNow(int page);
    // True when (x, y) falls on the dock, whose icons handle their own taps.
    bool DockContains(int x, int y) const;

    bool enabled() const { return enabled_; }
    bool night_mode() const { return night_mode_; }
    // Saved in NVS. Safe from any task.
    void SetEnabled(bool on);
    void SetNightMode(bool on);

    // Anything the user should see (a chat line, a notification) hides the home screen for a
    // while. Safe from any task.
    static void NoteActivity();
    static void SetMusicActive(bool active);

private:
    struct Digit {
        std::array<lv_obj_t*, 7> segments{};
    };

    static void OnTimer(lv_timer_t* timer);
    void Tick();
    void Build();
    void BuildClockPage();
    void BuildCalendarPage();
    void Show();
    void Hide();
    void UpdateClock(bool force);
    void UpdateWeather();
    void UpdateNight(bool night);
    void ApplyColors();
    void SetDigit(Digit& digit, int value);
    Digit CreateDigit(lv_obj_t* parent, int x, int y, int w, int h, int t);

    Hooks hooks_;
    bool started_ = false;
    bool built_ = false;
    bool shown_ = false;
    bool enabled_ = true;
    bool night_mode_ = true;
    bool night_ = false;   // night colours on
    bool dimmed_ = false;  // backlight lowered
    int64_t wake_until_us_ = 0;
    int page_ = 0;
    const lv_font_t* font_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* clock_page_ = nullptr;
    lv_obj_t* calendar_page_ = nullptr;

    // Clock page widgets.
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* lunar_label_ = nullptr;
    std::array<Digit, 4> clock_digits_{};
    std::array<lv_obj_t*, 2> colon_{};
    lv_obj_t* seconds_label_ = nullptr;
    lv_obj_t* weather_icon_ = nullptr;
    std::array<Digit, 2> temp_digits_{};
    lv_obj_t* degree_ = nullptr;
    lv_obj_t* weather_label_ = nullptr;
    lv_obj_t* detail_label_ = nullptr;
    std::array<lv_obj_t*, 3> forecast_labels_{};
    std::array<lv_obj_t*, 3> forecast_icons_{};
    std::vector<lv_obj_t*> text_labels_;  // recoloured at night
    lv_obj_t* dock_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> dock_handlers_;

    int shown_minute_ = -1;
    bool pomodoro_shown_ = false;
    int calendar_day_ = -1;
    int64_t weather_stamp_ = -1;
    lv_color_t on_color_{};
    lv_color_t off_color_{};

    static std::atomic<int64_t> last_activity_us_;
    static std::atomic<bool> music_active_;
};

#endif  // HOME_SCREEN_H
