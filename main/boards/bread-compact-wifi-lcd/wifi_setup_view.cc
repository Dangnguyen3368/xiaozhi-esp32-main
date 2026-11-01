#include "wifi_setup_view.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "study_ui.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cstring>

#define TAG "WifiSetupView"

namespace {
constexpr int kLockTimeoutMs = 3000;
constexpr int kButtonHeight = 34;
constexpr size_t kMaxNetworks = 12;
constexpr size_t kMaxPasswordLength = 63;
constexpr int kConfigModeWaitMs = 10000;
constexpr int kScanAttempts = 4;

const lv_color_t kInk = lv_color_hex(0x202020);
const lv_color_t kBlue = lv_color_hex(0x1E88E5);
const lv_color_t kGreen = lv_color_hex(0x43A047);
const lv_color_t kGrey = lv_color_hex(0x757575);

// Key labels. Special keys are matched by their text in OnKey.
constexpr const char* kKeyShift = "ABC";
constexpr const char* kKeyLower = "abc";
constexpr const char* kKeySymbols = "#+=";
constexpr const char* kKeyDelete = "Xóa";
constexpr const char* kKeySpace = "Cách";
constexpr const char* kKeyCancel = "Hủy";
constexpr const char* kKeyConnect = "Kết nối";

const char* const kLowerMap[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    kKeyShift, "z", "x", "c", "v", "b", "n", "m", kKeyDelete, "\n",
    kKeySymbols, kKeySpace, kKeyCancel, kKeyConnect, ""};
const char* const kUpperMap[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    kKeyLower, "Z", "X", "C", "V", "B", "N", "M", kKeyDelete, "\n",
    kKeySymbols, kKeySpace, kKeyCancel, kKeyConnect, ""};
const char* const kSymbolMap[] = {
    "!", "@", "#", "$", "%", "&", "*", "(", ")", "-", "\n",
    "_", "=", "+", ".", ",", "?", "/", ":", ";", "\n",
    "'", "\"", "<", ">", "[", "]", "{", "}", "~", "\n",
    kKeyLower, "^", "|", "\\", "`", kKeyDelete, "\n",
    kKeyShift, kKeySpace, kKeyCancel, kKeyConnect, ""};

const char* SignalText(int rssi) {
    if (rssi >= -60) {
        return "mạnh";
    }
    if (rssi >= -72) {
        return "vừa";
    }
    return "yếu";
}

void RestartSoon() {
    xTaskCreate(
        [](void*) {
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_restart();
        },
        "wifi_setup_restart", 2048, nullptr, 5, nullptr);
}
}  // namespace

void WifiSetupView::Start() {
    if (scanning_.exchange(true)) {
        return;  // a scan is already running and will show its result
    }
    shown_ = true;
    ShowMessage("Chọn Wi-Fi", "Đang quét mạng Wi-Fi, đợi vài giây...", false);
    if (xTaskCreate(ScanTask, "wifi_setup_scan", 6144, this, 3, nullptr) != pdPASS) {
        scanning_ = false;
        ShowMessage("Chọn Wi-Fi", "Không quét được. Thử lại sau.", true);
    }
}

void WifiSetupView::ScanTask(void* arg) {
    auto* self = static_cast<WifiSetupView*>(arg);
    auto& wifi = WifiManager::GetInstance();
    bool ready = wifi.IsConfigMode();
    if (!ready && self->enter_config_mode_) {
        // Frees the radio from the home network; the hotspot mode scans by itself.
        self->entered_config_mode_ = true;
        self->enter_config_mode_();
        for (int waited = 0; waited < kConfigModeWaitMs && !ready; waited += 250) {
            vTaskDelay(pdMS_TO_TICKS(250));
            ready = wifi.IsConfigMode();
        }
        // Let the hotspot finish its own first scan before starting ours.
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    const bool found = ready && self->Scan();
    self->scanning_ = false;
    Application::GetInstance().Schedule([self, found]() {
        if (!self->shown_) {
            return;
        }
        if (found) {
            self->ShowList();
        } else {
            self->ShowMessage("Chọn Wi-Fi", "Không tìm thấy mạng Wi-Fi nào. Bấm Quét lại.", true);
        }
    });
    vTaskDelete(nullptr);
}

bool WifiSetupView::Scan() {
    wifi_scan_config_t config = {};
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 100;
    config.scan_time.active.max = 300;

    for (int attempt = 0; attempt < kScanAttempts; ++attempt) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        // Blocking scan. The hotspot also scans now and then, so a busy radio just retries.
        esp_err_t err = esp_wifi_scan_start(&config, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
            continue;
        }
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        if (count == 0) {
            continue;
        }
        std::vector<wifi_ap_record_t> records(count);
        if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK || count == 0) {
            continue;  // the hotspot's own handler took the results first
        }
        records.resize(count);

        access_points_.clear();
        for (const auto& record : records) {
            const std::string ssid(reinterpret_cast<const char*>(record.ssid),
                                   strnlen(reinterpret_cast<const char*>(record.ssid), sizeof(record.ssid)));
            if (ssid.empty()) {
                continue;
            }
            auto it = std::find_if(access_points_.begin(), access_points_.end(),
                                   [&ssid](const AccessPoint& ap) { return ap.ssid == ssid; });
            if (it == access_points_.end()) {
                access_points_.push_back({ssid, record.rssi, record.authmode == WIFI_AUTH_OPEN});
            } else if (record.rssi > it->rssi) {
                it->rssi = record.rssi;
            }
        }
        std::sort(access_points_.begin(), access_points_.end(),
                  [](const AccessPoint& a, const AccessPoint& b) { return a.rssi > b.rssi; });
        if (access_points_.size() > kMaxNetworks) {
            access_points_.resize(kMaxNetworks);
        }
        ESP_LOGI(TAG, "Found %u networks", static_cast<unsigned>(access_points_.size()));
        if (!access_points_.empty()) {
            return true;
        }
    }
    return false;
}

void WifiSetupView::ClearPanel() {
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    password_label_ = nullptr;
    keyboard_ = nullptr;
    handlers_.clear();
}

void WifiSetupView::Hide() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    ClearPanel();
    shown_ = false;
    lvgl_port_unlock();
}

void WifiSetupView::Close() {
    Hide();
    if (entered_config_mode_) {
        // The view switched the robot to hotspot mode; restart to rejoin the saved network.
        auto* display = Board::GetInstance().GetDisplay();
        if (display != nullptr) {
            display->ShowNotification("Đang khởi động lại...", 3000);
        }
        RestartSoon();
    }
}

void WifiSetupView::ShowMessage(const std::string& title, const std::string& text, bool with_buttons) {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    BuildMessage(title, text, with_buttons);
    lvgl_port_unlock();
}

void WifiSetupView::ShowList() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    BuildList();
    lvgl_port_unlock();
}

void WifiSetupView::ShowPassword() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    BuildPassword();
    lvgl_port_unlock();
}

void WifiSetupView::BuildMessage(const std::string& title, const std::string& text, bool with_buttons) {
    ClearPanel();
    panel_ = study_ui::CreateOverlay();
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    study_ui::AddLabel(panel_, title, kBlue);
    lv_obj_t* body = study_ui::AddLabel(panel_, text, kInk);
    lv_obj_set_flex_grow(body, 1);

    lv_obj_t* row = lv_obj_create(panel_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kButtonHeight);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    auto add = [&](const char* label, lv_color_t color, std::function<void()> action) {
        lv_obj_t* button = study_ui::AddButton(row, label, color, std::move(action), handlers_);
        lv_obj_set_height(button, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
    };
    if (with_buttons) {
        add("Quét lại", kBlue, [this]() { Start(); });
    }
    add("Đóng", kGrey, [this]() { Close(); });
}

void WifiSetupView::BuildList() {
    ClearPanel();
    panel_ = study_ui::CreateOverlay();
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    study_ui::AddLabel(panel_, "Chọn Wi-Fi (vuốt để xem thêm)", kBlue);

    lv_obj_t* scroller = lv_obj_create(panel_);
    lv_obj_remove_style_all(scroller);
    lv_obj_set_width(scroller, LV_PCT(100));
    lv_obj_set_flex_grow(scroller, 1);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroller, 4, 0);

    for (const auto& ap : access_points_) {
        std::string text = ap.ssid + "  ·  " + SignalText(ap.rssi);
        if (ap.open) {
            text += ", không mật khẩu";
        }
        const std::string ssid = ap.ssid;
        const bool open = ap.open;
        lv_obj_t* button = study_ui::AddButton(scroller, text, kGreen, [this, ssid, open]() {
            ssid_ = ssid;
            password_.clear();
            if (open) {
                SaveAndRestart();
            } else {
                keyboard_page_ = 0;
                ShowPassword();
            }
        }, handlers_);
        lv_obj_set_size(button, LV_PCT(100), kButtonHeight);
        if (lv_obj_t* label = lv_obj_get_child(button, 0)) {
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(label, LV_PCT(100));
        }
    }

    lv_obj_t* row = lv_obj_create(panel_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kButtonHeight);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_t* rescan = study_ui::AddButton(row, "Quét lại", kBlue, [this]() { Start(); }, handlers_);
    lv_obj_set_height(rescan, LV_PCT(100));
    lv_obj_set_flex_grow(rescan, 1);
    lv_obj_t* close = study_ui::AddButton(row, "Đóng", kGrey, [this]() { Close(); }, handlers_);
    lv_obj_set_height(close, LV_PCT(100));
    lv_obj_set_flex_grow(close, 1);
}

void WifiSetupView::BuildPassword() {
    ClearPanel();
    panel_ = study_ui::CreateOverlay();
    lv_obj_set_style_pad_all(panel_, 3, 0);
    lv_obj_set_style_pad_row(panel_, 3, 0);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = study_ui::AddLabel(panel_, "Mật khẩu " + ssid_ + ":", kBlue);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    password_label_ = study_ui::AddLabel(panel_, "", kInk);
    lv_obj_set_style_bg_color(password_label_, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_bg_opa(password_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(password_label_, 3, 0);
    lv_label_set_long_mode(password_label_, LV_LABEL_LONG_DOT);
    UpdatePasswordLabel();

    keyboard_ = lv_buttonmatrix_create(panel_);
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_flex_grow(keyboard_, 1);
    lv_obj_set_style_pad_all(keyboard_, 2, 0);
    lv_obj_set_style_pad_row(keyboard_, 3, 0);
    lv_obj_set_style_pad_column(keyboard_, 3, 0);
    lv_obj_add_event_cb(keyboard_, OnKey, LV_EVENT_VALUE_CHANGED, this);
    SetKeyboardPage(keyboard_page_);
}

void WifiSetupView::SetKeyboardPage(int page) {
    keyboard_page_ = page;
    if (keyboard_ == nullptr) {
        return;
    }
    const char* const* map = page == 1 ? kUpperMap : page == 2 ? kSymbolMap : kLowerMap;
    lv_buttonmatrix_set_map(keyboard_, map);
}

void WifiSetupView::UpdatePasswordLabel() {
    if (password_label_ == nullptr) {
        return;
    }
    // Shown in clear so typos are easy to spot on a small keyboard.
    lv_label_set_text(password_label_, password_.empty() ? "(gõ mật khẩu)" : password_.c_str());
}

void WifiSetupView::OnKey(lv_event_t* e) {
    // Runs in the LVGL task with the lock held.
    auto* self = static_cast<WifiSetupView*>(lv_event_get_user_data(e));
    auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const uint32_t id = lv_buttonmatrix_get_selected_button(keyboard);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char* text = lv_buttonmatrix_get_button_text(keyboard, id);
    if (text == nullptr) {
        return;
    }
    const std::string key = text;
    if (key == kKeyShift) {
        self->SetKeyboardPage(1);
    } else if (key == kKeyLower) {
        self->SetKeyboardPage(0);
    } else if (key == kKeySymbols) {
        self->SetKeyboardPage(2);
    } else if (key == kKeyDelete) {
        if (!self->password_.empty()) {
            self->password_.pop_back();
        }
    } else if (key == kKeySpace) {
        if (self->password_.size() < kMaxPasswordLength) {
            self->password_ += ' ';
        }
    } else if (key == kKeyCancel) {
        Application::GetInstance().Schedule([self]() { self->ShowList(); });
        return;
    } else if (key == kKeyConnect) {
        if (self->password_.size() < 8) {
            lv_label_set_text(self->password_label_, "Mật khẩu Wi-Fi cần ít nhất 8 ký tự");
            return;
        }
        Application::GetInstance().Schedule([self]() { self->SaveAndRestart(); });
        return;
    } else if (self->password_.size() < kMaxPasswordLength) {
        self->password_ += key;
    }
    self->UpdatePasswordLabel();
}

void WifiSetupView::SaveAndRestart() {
    ESP_LOGI(TAG, "Saving Wi-Fi %s", ssid_.c_str());
    SsidManager::GetInstance().AddSsid(ssid_, password_);
    ShowMessage("Đã lưu Wi-Fi",
                "Robot đang khởi động lại để vào mạng " + ssid_ +
                    ".\n\nNếu sai mật khẩu, robot sẽ quay về chế độ cài Wi-Fi; khi đó mở Menu, chọn Wi-Fi để thử lại.",
                false);
    RestartSoon();
}
