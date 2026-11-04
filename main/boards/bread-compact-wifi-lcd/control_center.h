#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <lvgl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class HomeScreen;

// Quick settings opened by swiping down: volume, brightness, home screen and night mode
// switches, Wi-Fi, device info and restart. Show()/ShowInfo()/Hide() take the LVGL lock, so
// call them from the main task (not the LVGL task).
class ControlCenter {
public:
    ControlCenter(HomeScreen& home, std::function<void()> wifi_setup)
        : home_(home), wifi_setup_(std::move(wifi_setup)) {}

    void Show();
    void ShowInfo();
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    lv_obj_t* AddSliderRow(const char* name, int value, int min, int on_release_kind);
    static void OnSliderChanged(lv_event_t* e);
    static void OnSliderReleased(lv_event_t* e);
    bool Begin();

    HomeScreen& home_;
    std::function<void()> wifi_setup_;
    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::atomic<bool> shown_{false};
};

#endif  // CONTROL_CENTER_H
