#include "home_screen.h"

#include "application.h"
#include "backlight.h"
#include "board.h"
#include "lunar_calendar.h"
#include "app_icons.h"

LV_FONT_DECLARE(font_clock_86);
LV_FONT_DECLARE(font_clock_40);
#include "settings.h"
#include "study/study_timer.h"
#include "study_ui.h"
#include "weather.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#define TAG "HomeScreen"

std::atomic<int64_t> HomeScreen::last_activity_us_{0};
std::atomic<bool> HomeScreen::music_active_{false};

namespace {
// The home screen appears after this long without chat, notifications or other screens.
// After a chat, notification or music the chat screen stays this long, then the home screen
// (the main screen) comes back.
constexpr int64_t kIdleShowUs = 4LL * 1000 * 1000;
constexpr int64_t kNightWakeUs = 10LL * 1000 * 1000;
constexpr uint8_t kNightBrightness = 5;
constexpr int kNightStartHour = 22;
constexpr int kNightEndHour = 6;
constexpr int kPages = 2;  // clock, calendar
// The dock of app icons along the bottom of the clock page.
constexpr int kDockY = 252;
constexpr int kDockTile = 50;

// Day theme: deep navy with bright text (the non-IPS panel washes out mid greys).
const lv_color_t kDayBg = lv_color_hex(0x0B1020);
const lv_color_t kDayOn = lv_color_hex(0xEAF4FF);
const lv_color_t kDayOff = lv_color_hex(0x1A2335);
const lv_color_t kDayText = lv_color_hex(0xFFFFFF);
const lv_color_t kDaySub = lv_color_hex(0xC8D6E5);
// Night theme: black with a dim amber clock.
const lv_color_t kNightBg = lv_color_hex(0x000000);
const lv_color_t kNightOn = lv_color_hex(0xFFA000);
const lv_color_t kNightOff = lv_color_hex(0x140E00);
const lv_color_t kNightText = lv_color_hex(0xB07000);
const lv_color_t kAccent = lv_color_hex(0x42A5F5);
const lv_color_t kSunday = lv_color_hex(0xFF7070);
const lv_color_t kLunarFirst = lv_color_hex(0xFFB74D);


const char* const kWeekdayLong[] = {"Chủ nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
const char* const kWeekdayShort[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color, int radius) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

lv_obj_t* Container(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    return obj;
}

lv_obj_t* Label(lv_obj_t* parent, int x, int y, int w, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    if (w > 0) {
        lv_obj_set_width(label, w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    }
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, "");
    return label;
}

bool LocalTime(struct tm& tm_now) {
    const time_t now = time(nullptr);
    localtime_r(&now, &tm_now);
    return tm_now.tm_year + 1900 >= 2024;  // the clock is set from the server at boot
}

// Small weather picture from simple shapes, inside a box of `size` pixels (drawn for 40).
void DrawWeatherIcon(lv_obj_t* box, int code, int size) {
    lv_obj_clean(box);
    auto Box = [size](lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color, int radius) {
        const int r = radius == LV_RADIUS_CIRCLE ? radius : radius * size / 40;
        return ::Box(parent, x * size / 40, y * size / 40, std::max(1, w * size / 40), std::max(1, h * size / 40),
                     color, r);
    };
    const lv_color_t sun = lv_color_hex(0xFFC107);
    const lv_color_t cloud = code >= 95 ? lv_color_hex(0x78849E) : lv_color_hex(0xDDE6F0);
    const bool clear = code <= 1;
    const bool partly = code == 1 || code == 2;
    const bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    const bool storm = code >= 95;
    if (clear && code == 0) {
        Box(box, 8, 8, 24, 24, sun, LV_RADIUS_CIRCLE);
        return;
    }
    if (partly) {
        Box(box, 18, 2, 18, 18, sun, LV_RADIUS_CIRCLE);
    }
    Box(box, 4, 14, 18, 18, cloud, LV_RADIUS_CIRCLE);
    Box(box, 13, 7, 22, 22, cloud, LV_RADIUS_CIRCLE);
    Box(box, 4, 22, 32, 10, cloud, 5);
    if (rain) {
        const lv_color_t drop = lv_color_hex(0x4FC3F7);
        for (int i = 0; i < 3; ++i) {
            Box(box, 10 + i * 9, 34, 3, 6, drop, 1);
        }
    } else if (storm) {
        Box(box, 18, 32, 4, 8, sun, 1);
    }
}
}  // namespace

void HomeScreen::NoteActivity() { last_activity_us_ = esp_timer_get_time(); }

void HomeScreen::SetMusicActive(bool active) {
    music_active_ = active;
    NoteActivity();
}

void HomeScreen::SetEnabled(bool on) {
    enabled_ = on;
    Settings settings("home", true);
    settings.SetBool("enabled", on);
}

void HomeScreen::SetNightMode(bool on) {
    night_mode_ = on;
    Settings settings("home", true);
    settings.SetBool("night", on);
}

void HomeScreen::Start(Hooks hooks) {
    if (started_) {
        return;
    }
    started_ = true;
    hooks_ = std::move(hooks);
    Settings settings("home");
    enabled_ = settings.GetBool("enabled", true);
    night_mode_ = settings.GetBool("night", true);
    lv_timer_create(OnTimer, 1000, this);
}

void HomeScreen::OnTimer(lv_timer_t* timer) { static_cast<HomeScreen*>(lv_timer_get_user_data(timer))->Tick(); }

void HomeScreen::Tick() {
    auto& app = Application::GetInstance();
    const DeviceState state = app.GetDeviceState();
    if (!built_) {
        // Wait for the first idle state: by then the assets (and their text font) are loaded,
        // so this long-lived screen never points at a font that is freed later.
        if (state != kDeviceStateIdle) {
            return;
        }
        Build();
    }
    if (const lv_font_t* font = study_ui::TextFont(); font != nullptr && font != font_) {
        font_ = font;
        lv_obj_set_style_text_font(root_, font_, 0);
    }

    const bool overlay = hooks_.overlay_shown && hooks_.overlay_shown();
    // Menus and other screens only cover the home screen; once closed it is back at once.
    if (state != kDeviceStateIdle) {
        NoteActivity();
    }
    const int64_t now = esp_timer_get_time();
    const bool want = enabled_ && state == kDeviceStateIdle && !music_active_ && !overlay &&
                      now - last_activity_us_ >= kIdleShowUs;
    if (want && !shown_) {
        Show();
    } else if (!want && shown_) {
        Hide();
    }
    if (!shown_) {
        return;
    }

    struct tm tm_now = {};
    const bool valid = LocalTime(tm_now);
    UpdateNight(night_mode_ && valid && (tm_now.tm_hour >= kNightStartHour || tm_now.tm_hour < kNightEndHour));
    UpdateClock(false);
    UpdateWeather();
    if (page_ == 1 && valid && calendar_day_ != tm_now.tm_yday) {
        BuildCalendarPage();
    }
}

void HomeScreen::Build() {
    built_ = true;
    font_ = study_ui::TextFont();
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    if (font_ != nullptr) {
        lv_obj_set_style_text_font(root_, font_, 0);
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    clock_page_ = Container(root_);
    BuildClockPage();
    ApplyColors();
    ESP_LOGI(TAG, "Home screen ready");
}

// Laid out for the portrait 240x320 TFT of this board: date, clock, weather, forecast and a
// dock of four app icons.
void HomeScreen::BuildClockPage() {
    date_label_ = Label(clock_page_, 8, 4, 140, kDayText);
    lunar_label_ = Label(clock_page_, 140, 4, 94, kDaySub);
    lv_obj_set_style_text_align(lunar_label_, LV_TEXT_ALIGN_RIGHT, 0);
    text_labels_ = {date_label_};

    // Big solid digits (Inter Display), each centred in its own cell so the clock never shifts.
    constexpr int kCell = 54, kColon = 22, kY = 18, kX = 3;
    const int xs[4] = {kX, kX + kCell, kX + 2 * kCell + kColon, kX + 3 * kCell + kColon};
    for (int i = 0; i < 4; ++i) {
        clock_digits_[i] = lv_label_create(clock_page_);
        lv_obj_set_style_text_font(clock_digits_[i], &font_clock_86, 0);
        lv_obj_set_style_text_align(clock_digits_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(clock_digits_[i], kCell);
        lv_obj_set_pos(clock_digits_[i], xs[i], kY);
        lv_label_set_text(clock_digits_[i], "-");
    }
    colon_ = lv_label_create(clock_page_);
    lv_obj_set_style_text_font(colon_, &font_clock_86, 0);
    lv_obj_set_style_text_align(colon_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(colon_, kColon);
    lv_obj_set_pos(colon_, kX + 2 * kCell, kY - 6);
    lv_label_set_text(colon_, ":");
    seconds_label_ = Label(clock_page_, 214, 108, 0, kDaySub);
    lv_obj_set_style_text_font(seconds_label_, LV_FONT_DEFAULT, 0);

    weather_icon_ = lv_obj_create(clock_page_);
    lv_obj_remove_style_all(weather_icon_);
    lv_obj_remove_flag(weather_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(weather_icon_, 8, 132);
    lv_obj_set_size(weather_icon_, 40, 40);
    temp_label_ = lv_label_create(clock_page_);
    lv_obj_set_style_text_font(temp_label_, &font_clock_40, 0);
    lv_obj_set_pos(temp_label_, 54, 126);
    lv_label_set_text(temp_label_, "");
    weather_label_ = lv_label_create(clock_page_);  // two short lines: sky, then min-max
    lv_obj_set_pos(weather_label_, 128, 128);
    lv_obj_set_width(weather_label_, 108);
    lv_label_set_long_mode(weather_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(weather_label_, "");
    detail_label_ = Label(clock_page_, 8, 176, 226, kDaySub);
    // Next three days: a small picture beside "T5" over "20-25".
    for (int i = 0; i < 3; ++i) {
        forecast_icons_[i] = lv_obj_create(clock_page_);
        lv_obj_remove_style_all(forecast_icons_[i]);
        lv_obj_remove_flag(forecast_icons_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(forecast_icons_[i], 6 + i * 78, 204);
        lv_obj_set_size(forecast_icons_[i], 24, 24);
        forecast_labels_[i] = Label(clock_page_, 34 + i * 78, 200, 46, kDaySub);
        lv_obj_set_style_text_font(forecast_labels_[i], LV_FONT_DEFAULT, 0);
    }
    text_labels_.push_back(weather_label_);

    // Dock: four app icons without labels, evenly spread.
    dock_ = lv_obj_create(clock_page_);
    lv_obj_remove_style_all(dock_);
    lv_obj_remove_flag(dock_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(dock_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(dock_, 0, kDockY - 8);
    lv_obj_set_size(dock_, LV_PCT(100), 320 - kDockY + 8);
    lv_obj_set_style_bg_color(dock_, lv_color_hex(0x161E33), 0);
    lv_obj_set_style_bg_opa(dock_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dock_, 16, 0);
    const int count = std::min<int>(hooks_.dock.size(), 4);
    if (count > 0) {
        const int screen_w = lv_obj_get_width(lv_layer_top());
        const int cell_w = screen_w / count;
        for (int i = 0; i < count; ++i) {
            CreateAppTile(dock_, i * cell_w, 10, kDockTile, cell_w, hooks_.dock[i], std::string(), dock_handlers_);
        }
    }
}

void HomeScreen::BuildCalendarPage() {
    struct tm tm_now = {};
    const bool valid = LocalTime(tm_now);
    if (calendar_page_ != nullptr) {
        lv_obj_delete(calendar_page_);
        calendar_page_ = nullptr;
    }
    calendar_page_ = Container(root_);
    if (page_ != 1) {
        lv_obj_add_flag(calendar_page_, LV_OBJ_FLAG_HIDDEN);
    }
    const lv_color_t text = night_ ? kNightText : kDayText;
    const lv_color_t sub = night_ ? kNightText : kDaySub;
    if (!valid) {
        lv_obj_t* label = Label(calendar_page_, 10, 140, 220, text);
        lv_label_set_text(label, "Chưa có giờ từ máy chủ.");
        calendar_day_ = -1;
        return;
    }
    calendar_day_ = tm_now.tm_yday;
    const int year = tm_now.tm_year + 1900;
    const int month = tm_now.tm_mon + 1;
    const auto today_lunar = lunar::FromSolar(tm_now.tm_mday, month, year);

    char buffer[96];
    lv_obj_t* title = Label(calendar_page_, 6, 4, 80, text);
    snprintf(buffer, sizeof(buffer), "%d/%d", month, year);
    lv_label_set_text(title, buffer);
    lv_obj_t* lunar_title = Label(calendar_page_, 80, 4, 156, sub);
    lv_obj_set_style_text_align(lunar_title, LV_TEXT_ALIGN_RIGHT, 0);
    snprintf(buffer, sizeof(buffer), "Âm %d/%d%s %s", today_lunar.day, today_lunar.month, today_lunar.leap ? "n" : "",
             lunar::YearName(today_lunar.year).c_str());
    lv_label_set_text(lunar_title, buffer);

    constexpr int kLeft = 3, kCellW = 33, kTop = 46, kBottom = 318;
    for (int col = 0; col < 7; ++col) {
        lv_obj_t* label = Label(calendar_page_, kLeft + col * kCellW, 28, kCellW, col == 6 ? kSunday : sub);
        lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, kWeekdayShort[(col + 1) % 7]);
    }

    // Monday-first grid.
    struct tm first = tm_now;
    first.tm_mday = 1;
    first.tm_hour = 12;
    mktime(&first);
    const int lead = (first.tm_wday + 6) % 7;
    static const int kDaysIn[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = kDaysIn[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        days = 29;
    }
    const int rows = (lead + days + 6) / 7;
    const int cell_h = (kBottom - kTop) / rows;
    for (int day = 1; day <= days; ++day) {
        const int index = lead + day - 1;
        const int col = index % 7;
        const int row = index / 7;
        const bool today = day == tm_now.tm_mday;
        lv_obj_t* cell = Box(calendar_page_, kLeft + col * kCellW + 1, kTop + row * cell_h, kCellW - 2, cell_h - 2,
                             today ? kAccent : (night_ ? kNightBg : kDayBg), 6);
        lv_obj_t* solar = lv_label_create(cell);
        snprintf(buffer, sizeof(buffer), "%d", day);
        lv_label_set_text(solar, buffer);
        lv_obj_set_style_text_color(solar, today ? lv_color_white() : (col == 6 ? kSunday : text), 0);
        lv_obj_align(solar, LV_ALIGN_TOP_LEFT, 2, 2);

        const auto lunar_day = lunar::FromSolar(day, month, year);
        lv_obj_t* small = lv_label_create(cell);
        lv_obj_set_style_text_font(small, LV_FONT_DEFAULT, 0);
        if (lunar_day.day == 1) {
            snprintf(buffer, sizeof(buffer), "%d/%d", lunar_day.day, lunar_day.month);
        } else {
            snprintf(buffer, sizeof(buffer), "%d", lunar_day.day);
        }
        lv_label_set_text(small, buffer);
        lv_obj_set_style_text_color(small,
                                    today ? lv_color_white() : (lunar_day.day == 1 || lunar_day.day == 15 ? kLunarFirst : sub),
                                    0);
        lv_obj_align(small, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
    }
}

void HomeScreen::Show() {
    shown_ = true;
    page_ = 0;
    lv_obj_remove_flag(clock_page_, LV_OBJ_FLAG_HIDDEN);
    if (calendar_page_ != nullptr) {
        lv_obj_add_flag(calendar_page_, LV_OBJ_FLAG_HIDDEN);
    }
    UpdateClock(true);
    weather_stamp_ = -1;
    UpdateWeather();
    ApplyColors();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (hooks_.floating_buttons) {
        hooks_.floating_buttons(true);
    }
}

void HomeScreen::Hide() {
    shown_ = false;
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    if (calendar_page_ != nullptr) {
        lv_obj_delete(calendar_page_);
        calendar_page_ = nullptr;
        calendar_day_ = -1;
    }
    UpdateNight(false);
    if (hooks_.floating_buttons) {
        hooks_.floating_buttons(false);
    }
}

bool HomeScreen::HandleTap() {
    if (!shown_) {
        return false;
    }
    if (dimmed_) {
        wake_until_us_ = esp_timer_get_time() + kNightWakeUs;
        UpdateNight(night_);
        return true;
    }
    return false;
}

void HomeScreen::ShowNow(int page) {
    if (!lvgl_port_lock(3000)) {
        return;
    }
    last_activity_us_ = 0;  // counts as idle for long enough
    if (built_ && Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        if (!shown_) {
            Show();
        }
        ChangePage(page - page_);
    }
    lvgl_port_unlock();
}

bool HomeScreen::DockContains(int x, int y) const {
    (void)x;
    return shown_ && page_ == 0 && !night_ && y >= kDockY - 8;
}

void HomeScreen::Dismiss() {
    NoteActivity();
    if (shown_) {
        Hide();
    }
}

void HomeScreen::ChangePage(int delta) {
    if (!shown_) {
        return;
    }
    const int page = std::clamp(page_ + delta, 0, kPages - 1);
    if (page == page_) {
        return;
    }
    page_ = page;
    if (page_ == 1) {
        BuildCalendarPage();
        lv_obj_add_flag(clock_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(calendar_page_, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (calendar_page_ != nullptr) {
            lv_obj_add_flag(calendar_page_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(clock_page_, LV_OBJ_FLAG_HIDDEN);
    }
    ApplyColors();
}

void HomeScreen::UpdateNight(bool night) {
    if (night != night_) {
        night_ = night;
        ApplyColors();
        if (page_ == 1) {
            BuildCalendarPage();
            lv_obj_remove_flag(calendar_page_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool dim = night_ && shown_ && esp_timer_get_time() >= wake_until_us_;
    Backlight* backlight = Board::GetInstance().GetBacklight();
    if (backlight == nullptr) {
        return;
    }
    if (dim && !dimmed_) {
        backlight->SetBrightness(kNightBrightness);
        dimmed_ = true;
    } else if (!dim && dimmed_) {
        backlight->RestoreBrightness();
        dimmed_ = false;
    }
}

void HomeScreen::ApplyColors() {
    if (root_ == nullptr) {
        return;
    }
    on_color_ = night_ ? kNightOn : kDayOn;
    off_color_ = night_ ? kNightOff : kDayOff;
    for (lv_obj_t* digit : clock_digits_) {
        lv_obj_set_style_text_color(digit, on_color_, 0);
    }
    lv_obj_set_style_text_color(temp_label_, on_color_, 0);
    const lv_color_t text = night_ ? kNightText : kDayText;
    const lv_color_t sub = night_ ? kNightText : kDaySub;
    lv_obj_set_style_bg_color(root_, night_ ? kNightBg : kDayBg, 0);
    // At night only the date and clock show, so move them to the middle of the screen.
    lv_obj_set_y(clock_page_, night_ ? 100 : 0);
    for (lv_obj_t* label : text_labels_) {
        lv_obj_set_style_text_color(label, text, 0);
    }
    for (lv_obj_t* label : {lunar_label_, seconds_label_, detail_label_}) {
        lv_obj_set_style_text_color(label, sub, 0);
    }
    for (lv_obj_t* label : forecast_labels_) {
        lv_obj_set_style_text_color(label, sub, 0);
    }
    // At night only the clock stays; the weather would be too bright.
    for (lv_obj_t* obj : {weather_icon_, temp_label_, weather_label_, detail_label_, forecast_labels_[0],
                          forecast_labels_[1], forecast_labels_[2], forecast_icons_[0], forecast_icons_[1],
                          forecast_icons_[2], dock_}) {
        if (night_) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    UpdateClock(true);
    weather_stamp_ = -1;
    UpdateWeather();
}

void HomeScreen::UpdateClock(bool force) {
    if (clock_page_ == nullptr) {
        return;
    }
    struct tm tm_now = {};
    const bool valid = LocalTime(tm_now);
    const bool colon_on = !valid || tm_now.tm_sec % 2 == 0;
    lv_obj_set_style_text_color(colon_, colon_on ? on_color_ : off_color_, 0);
    char buffer[96];
    if (valid) {
        snprintf(buffer, sizeof(buffer), "%02d", tm_now.tm_sec);
        lv_label_set_text(seconds_label_, buffer);
    } else {
        lv_label_set_text(seconds_label_, "");
    }

    // The right-hand corner shows the Pomodoro countdown while it runs.
    const auto pomodoro = StudyTimer::GetInstance().GetState();
    if (pomodoro.running) {
        snprintf(buffer, sizeof(buffer), "%s %02d:%02d", pomodoro.on_break ? "Nghỉ" : "Học",
                 pomodoro.remaining_seconds / 60, pomodoro.remaining_seconds % 60);
        lv_label_set_text(lunar_label_, buffer);
    } else if (pomodoro_shown_) {
        force = true;  // put the lunar date back
    }
    pomodoro_shown_ = pomodoro.running;

    const int minute = valid ? tm_now.tm_hour * 60 + tm_now.tm_min : -2;
    if (!force && minute == shown_minute_) {
        return;
    }
    shown_minute_ = minute;
    if (!valid) {
        for (lv_obj_t* digit : clock_digits_) {
            lv_label_set_text(digit, "-");
        }
        lv_label_set_text(date_label_, "Đang lấy giờ...");
        if (!pomodoro.running) {
            lv_label_set_text(lunar_label_, "");
        }
        return;
    }
    const int digits[4] = {tm_now.tm_hour / 10, tm_now.tm_hour % 10, tm_now.tm_min / 10, tm_now.tm_min % 10};
    for (int i = 0; i < 4; ++i) {
        const char text[2] = {static_cast<char>('0' + digits[i]), '\0'};
        lv_label_set_text(clock_digits_[i], text);
    }
    snprintf(buffer, sizeof(buffer), "%s, %02d/%02d", kWeekdayLong[tm_now.tm_wday], tm_now.tm_mday,
             tm_now.tm_mon + 1);
    lv_label_set_text(date_label_, buffer);
    if (!pomodoro.running) {
        const auto lunar_date = lunar::FromSolar(tm_now.tm_mday, tm_now.tm_mon + 1, tm_now.tm_year + 1900);
        snprintf(buffer, sizeof(buffer), "Âm %d/%d%s", lunar_date.day, lunar_date.month, lunar_date.leap ? "n" : "");
        lv_label_set_text(lunar_label_, buffer);
    }
}

void HomeScreen::UpdateWeather() {
    if (weather_label_ == nullptr) {
        return;
    }
    const Weather::Snapshot weather = Weather::GetInstance().Get();
    const int64_t stamp = weather.valid ? weather.fetched_us : 0;
    if (stamp == weather_stamp_) {
        return;
    }
    weather_stamp_ = stamp;
    if (!weather.valid) {
        lv_obj_clean(weather_icon_);
        lv_label_set_text(temp_label_, "--");
        lv_label_set_text(weather_label_, "Đang tải thời tiết...");
        lv_label_set_text(detail_label_, "");
        for (size_t i = 0; i < forecast_labels_.size(); ++i) {
            lv_label_set_text(forecast_labels_[i], "");
            lv_obj_clean(forecast_icons_[i]);
        }
        return;
    }
    DrawWeatherIcon(weather_icon_, weather.code, 40);
    const int temperature = std::clamp(static_cast<int>(weather.temperature + (weather.temperature >= 0 ? 0.5f : -0.5f)), -9, 99);
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%d°", temperature);
    lv_label_set_text(temp_label_, buffer);
    if (!weather.days.empty()) {
        snprintf(buffer, sizeof(buffer), "%s\n%d-%d", Weather::Describe(weather.code), weather.days[0].t_min,
                 weather.days[0].t_max);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", Weather::Describe(weather.code));
    }
    lv_label_set_text(weather_label_, buffer);
    snprintf(buffer, sizeof(buffer), "Ẩm %d%% · gió %d km/h", weather.humidity,
             static_cast<int>(weather.wind_kmh + 0.5f));
    lv_label_set_text(detail_label_, buffer);
    for (size_t i = 0; i < forecast_labels_.size(); ++i) {
        if (i + 1 < weather.days.size()) {
            const auto& day = weather.days[i + 1];
            snprintf(buffer, sizeof(buffer), "%s\n%d-%d", kWeekdayShort[day.weekday % 7], day.t_min, day.t_max);
            lv_label_set_text(forecast_labels_[i], buffer);
            DrawWeatherIcon(forecast_icons_[i], day.code, 24);
        } else {
            lv_label_set_text(forecast_labels_[i], "");
            lv_obj_clean(forecast_icons_[i]);
        }
    }
}
