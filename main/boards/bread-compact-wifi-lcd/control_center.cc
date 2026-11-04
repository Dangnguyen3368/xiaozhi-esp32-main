#include "control_center.h"

#include "application.h"
#include "audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "home_screen.h"
#include "study_ui.h"
#include "system_info.h"

#include <driver/temperature_sensor.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <wifi_manager.h>

#include <cstdio>
#include <string>

#define TAG "ControlCenter"

namespace {
constexpr int kLockTimeoutMs = 3000;
const lv_color_t kInk = lv_color_hex(0x202020);
const lv_color_t kBlue = lv_color_hex(0x1E88E5);
const lv_color_t kGreen = lv_color_hex(0x43A047);
const lv_color_t kTeal = lv_color_hex(0x00897B);
const lv_color_t kRed = lv_color_hex(0xE53935);
const lv_color_t kGrey = lv_color_hex(0x757575);
const lv_color_t kPurple = lv_color_hex(0x8E24AA);

enum SliderKind { kVolume = 0, kBrightness = 1 };

float ChipTemperature() {
    static temperature_sensor_handle_t sensor = nullptr;
    if (sensor == nullptr) {
        temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
        if (temperature_sensor_install(&config, &sensor) != ESP_OK || temperature_sensor_enable(sensor) != ESP_OK) {
            sensor = nullptr;
            return 0;
        }
    }
    float celsius = 0;
    temperature_sensor_get_celsius(sensor, &celsius);
    return celsius;
}

lv_obj_t* Row(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

void SliderText(lv_obj_t* label, const char* name, int value) {
    char text[48];
    snprintf(text, sizeof(text), "%s %d%%", name, value);
    lv_label_set_text(label, text);
}
}  // namespace

bool ControlCenter::Begin() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return false;
    }
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    handlers_.clear();
    panel_ = study_ui::CreateOverlay();
    return true;
}

void ControlCenter::Hide() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    handlers_.clear();
    shown_ = false;
    lvgl_port_unlock();
}

void ControlCenter::OnSliderChanged(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const char* name = lv_obj_get_user_data(slider) == reinterpret_cast<void*>(kVolume) ? "Âm lượng" : "Độ sáng";
    SliderText(label, name, lv_slider_get_value(slider));
}

void ControlCenter::OnSliderReleased(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const int value = lv_slider_get_value(slider);
    const bool volume = lv_obj_get_user_data(slider) == reinterpret_cast<void*>(kVolume);
    Application::GetInstance().Schedule([volume, value]() {
        auto& board = Board::GetInstance();
        if (volume) {
            board.GetAudioCodec()->SetOutputVolume(value);
        } else if (Backlight* backlight = board.GetBacklight()) {
            backlight->SetBrightness(value, true);
        }
    });
}

lv_obj_t* ControlCenter::AddSliderRow(const char* name, int value, int min, int kind) {
    lv_obj_t* row = Row(panel_);
    lv_obj_t* label = lv_label_create(row);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, kInk, 0);
    SliderText(label, name, value);
    lv_obj_t* slider = lv_slider_create(row);
    lv_obj_set_width(slider, LV_PCT(88));
    lv_obj_set_height(slider, 14);
    lv_obj_set_style_margin_left(slider, 12, 0);  // room for the knob
    lv_obj_set_style_margin_bottom(slider, 6, 0);
    lv_slider_set_range(slider, min, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_user_data(slider, reinterpret_cast<void*>(kind));
    lv_obj_add_event_cb(slider, OnSliderChanged, LV_EVENT_VALUE_CHANGED, label);
    lv_obj_add_event_cb(slider, OnSliderReleased, LV_EVENT_RELEASED, nullptr);
    return slider;
}

void ControlCenter::Show() {
    auto& board = Board::GetInstance();
    const int volume = board.GetAudioCodec()->output_volume();
    Backlight* backlight = board.GetBacklight();
    const int brightness = backlight != nullptr ? backlight->brightness() : 100;
    const bool home_on = home_.enabled();
    const bool night_on = home_.night_mode();
    if (!Begin()) {
        return;
    }
    lv_obj_set_style_pad_row(panel_, 8, 0);
    study_ui::AddLabel(panel_, "Bảng điều khiển", kInk);
    AddSliderRow("Âm lượng", volume, 0, kVolume);
    if (backlight != nullptr) {
        AddSliderRow("Độ sáng", brightness, 10, kBrightness);
    }

    lv_obj_t* grid = Row(panel_);
    auto add = [&](const std::string& text, lv_color_t color, std::function<void()> action) {
        lv_obj_t* button = study_ui::AddButton(grid, text, color, std::move(action), handlers_);
        lv_obj_set_size(button, LV_PCT(48), 44);
        if (lv_obj_t* label = lv_obj_get_child(button, 0)) {
            lv_obj_set_width(label, LV_PCT(100));
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        }
    };
    add(home_on ? "Màn chờ: Bật" : "Màn chờ: Tắt", home_on ? kGreen : kGrey, [this, home_on]() {
        home_.SetEnabled(!home_on);
        Show();
    });
    add(night_on ? "Chế độ đêm: Bật" : "Chế độ đêm: Tắt", night_on ? kPurple : kGrey, [this, night_on]() {
        home_.SetNightMode(!night_on);
        Show();
    });
    add("Wi-Fi", kTeal, [this]() {
        Hide();
        if (wifi_setup_) {
            wifi_setup_();
        }
    });
    add("Thông tin máy", kBlue, [this]() { ShowInfo(); });
    add("Khởi động lại", kRed, []() { esp_restart(); });
    add("Đóng", kGrey, [this]() { Hide(); });

    shown_ = true;
    lvgl_port_unlock();
}

void ControlCenter::ShowInfo() {
    auto& wifi = WifiManager::GetInstance();
    char line[128];
    std::vector<std::string> lines;
    snprintf(line, sizeof(line), "Phiên bản: %s", esp_app_get_description()->version);
    lines.push_back(line);
    if (wifi.IsConnected()) {
        snprintf(line, sizeof(line), "Wi-Fi: %s (%d dBm)", wifi.GetSsid().c_str(), wifi.GetRssi());
        lines.push_back(line);
        lines.push_back("IP: " + wifi.GetIpAddress());
    } else {
        lines.push_back("Wi-Fi: chưa kết nối");
    }
    snprintf(line, sizeof(line), "RAM: %u KB trống (thấp nhất %u KB)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    lines.push_back(line);
    const size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram > 0) {
        snprintf(line, sizeof(line), "PSRAM: %u KB trống / %u KB",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 static_cast<unsigned>(psram / 1024));
        lines.push_back(line);
    }
    if (const esp_partition_t* running = esp_ota_get_running_partition()) {
        snprintf(line, sizeof(line), "Phân vùng: %s (%u KB)", running->label,
                 static_cast<unsigned>(running->size / 1024));
        lines.push_back(line);
    }
    snprintf(line, sizeof(line), "Nhiệt độ chip: %.1f độ C", ChipTemperature());
    lines.push_back(line);
    const int64_t minutes = esp_timer_get_time() / 60000000LL;
    snprintf(line, sizeof(line), "Đã chạy: %d giờ %d phút", static_cast<int>(minutes / 60),
             static_cast<int>(minutes % 60));
    lines.push_back(line);
    lines.push_back("MAC: " + SystemInfo::GetMacAddress());

    if (!Begin()) {
        return;
    }
    lv_obj_set_style_pad_row(panel_, 2, 0);
    study_ui::AddLabel(panel_, "Thông tin máy", kBlue);
    for (const auto& text : lines) {
        study_ui::AddLabel(panel_, text, kInk);
    }
    lv_obj_t* back = study_ui::AddButton(panel_, "Quay lại", kGrey, [this]() { Show(); }, handlers_);
    lv_obj_set_size(back, LV_PCT(100), 36);
    shown_ = true;
    lvgl_port_unlock();
}
