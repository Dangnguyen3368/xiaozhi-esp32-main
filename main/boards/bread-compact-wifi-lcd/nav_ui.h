#ifndef NAV_UI_H
#define NAV_UI_H

#include <lvgl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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

#endif  // NAV_UI_H
