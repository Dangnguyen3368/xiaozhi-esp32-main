#ifndef NAV_UI_H
#define NAV_UI_H

#include <lvgl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nav/nav_assistant.h"

// Small pill with the car speed, the limit and the distance to the next camera (car mode).
// Hidden while no GPS module is wired. Update() may be called from any task but the LVGL one.
class NavBadge {
public:
    void Update(const nav::Status& status);

private:
    lv_obj_t* label_ = nullptr;
    std::string shown_text_;
    uint32_t shown_color_ = 0;
};

// Full-screen card with the QR code of the car settings page (http://<robot>:8081/xe).
class NavSetupView {
public:
    void Show();
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    bool BuildQr(const std::string& text, int max_px);

    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::vector<uint16_t> qr_pixels_;
    lv_image_dsc_t qr_dsc_ = {};
    std::atomic<bool> shown_{false};
};

// Seven-segment digit made of plain rectangles, so the speed can be huge without a big font.
class SegmentDigit {
public:
    void Create(lv_obj_t* parent, int x, int y, int w, int h, int thickness, lv_color_t color);
    // 0..9, -1 blank, 10 dash.
    void Set(int value);
    void SetColor(lv_color_t color);

private:
    lv_obj_t* segments_[7] = {};
};

// Driving screen: a speed page (big speed, limit sign, camera countdown) and a map page (TomTom
// raster tiles around the car with an arrow and camera dots). A tap switches pages. Works without
// a GPS module too: the map then shows the area around the saved place "Nhà".
class DriveView {
public:
    enum class Page { kSpeed, kMap };

    void Show(Page page);
    void Hide();
    bool IsShown() const { return shown_.load(); }
    // From the nav task, about once a second.
    void Update(const nav::Status& status);

private:
    void Build(Page page);
    void UpdateSpeedPageLocked(const nav::Status& status);
    void UpdateMapLabelsLocked(const nav::Status& status);
    static void MapTaskEntry(void* arg);
    void MapTask();
    void RenderMap();
    void SetAlertLocked(const nav::Status& status);
    void StopBlinkLocked();
    static void BlinkTimer(lv_timer_t* timer);

    std::atomic<bool> shown_{false};
    std::atomic<bool> map_active_{false};
    std::atomic<bool> map_dirty_{true};
    std::atomic<int> zoom_{16};
    Page page_ = Page::kSpeed;
    int width_ = 240;
    int height_ = 320;
    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    // Red camera icon and red screen border that blink near a camera (and the border when speeding).
    lv_obj_t* alert_icon_ = nullptr;
    lv_timer_t* blink_timer_ = nullptr;
    bool camera_alert_ = false;
    bool speed_alert_ = false;
    bool blink_on_ = false;

    // Speed page.
    lv_obj_t* gps_label_ = nullptr;
    SegmentDigit speed_digits_[3];
    SegmentDigit limit_digits_[3];
    lv_obj_t* camera_label_ = nullptr;
    lv_obj_t* camera_bar_ = nullptr;
    lv_obj_t* note_label_ = nullptr;

    // Map page.
    lv_obj_t* map_image_ = nullptr;
    lv_obj_t* map_speed_ = nullptr;
    lv_obj_t* map_info_ = nullptr;
    lv_image_dsc_t map_dsc_ = {};
    std::vector<uint16_t> map_front_;  // shown by map_image_, written under the LVGL lock
    std::vector<uint16_t> map_back_;   // drawn by the map task
    std::string map_message_;
    void* map_task_ = nullptr;

    std::mutex status_mutex_;
    nav::Status status_;
};

// Small "GPS Xe" button at the bottom left of the main screen that opens the driving screen.
class DriveButton {
public:
    static constexpr int kWidth = 64;
    static constexpr int kHeight = 30;
    static constexpr int kMargin = 4;
    // Call with the LVGL lock held, after the display exists.
    void Create(std::function<void()> on_click);
    static bool Contains(int x, int y, int screen_width, int screen_height);

private:
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
};

#endif  // NAV_UI_H
