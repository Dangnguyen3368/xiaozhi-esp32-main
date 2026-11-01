#ifndef WIFI_SETUP_VIEW_H
#define WIFI_SETUP_VIEW_H

#include <lvgl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Touch screen Wi-Fi setup, so no phone is needed: scans for networks, lists them, and takes
// the password on an on-screen keyboard. The choice is saved as the first Wi-Fi to try and the
// robot restarts to connect with it.
//
// Scanning needs the Wi-Fi radio free, so the view first puts the robot in Wi-Fi config mode
// (the same hotspot mode the phone setup uses) through `enter_config_mode`.
class WifiSetupView {
public:
    explicit WifiSetupView(std::function<void()> enter_config_mode)
        : enter_config_mode_(std::move(enter_config_mode)) {}

    // Opens the view and starts scanning. Call from the main task.
    void Start();
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    struct AccessPoint {
        std::string ssid;
        int rssi;
        bool open;
    };

    static void ScanTask(void* arg);
    bool Scan();
    static bool FetchHotspotList(std::string& body);

    // Each builder replaces what is on screen; call with the LVGL lock held.
    void BuildMessage(const std::string& title, const std::string& text, bool with_buttons);
    void BuildList();
    void BuildPassword();
    void ShowMessage(const std::string& title, const std::string& text, bool with_buttons);
    void ShowList();
    void ShowPassword();
    void Close();
    void SaveAndRestart();
    void ClearPanel();
    void UpdatePasswordLabel();
    void SetKeyboardPage(int page);
    static void OnKey(lv_event_t* e);

    std::function<void()> enter_config_mode_;
    std::vector<AccessPoint> access_points_;
    std::string ssid_;
    std::string password_;
    int keyboard_page_ = 0;  // 0 lower case, 1 upper case, 2 symbols
    bool entered_config_mode_ = false;

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* password_label_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::atomic<bool> shown_{false};
    std::atomic<bool> scanning_{false};
};

#endif  // WIFI_SETUP_VIEW_H
