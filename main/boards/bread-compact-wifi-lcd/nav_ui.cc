#include "nav_ui.h"

#include "study_ui.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#include <algorithm>

#define TAG "NavUi"

namespace {
constexpr int kLockTimeoutMs = 3000;
const lv_color_t kInk = lv_color_hex(0x202020);
const lv_color_t kBlue = lv_color_hex(0x1E88E5);
const lv_color_t kGrey = lv_color_hex(0x757575);
constexpr uint32_t kTealHex = 0x00897B;
constexpr uint32_t kOrangeHex = 0xFB8C00;
constexpr uint32_t kRedHex = 0xE53935;
constexpr uint32_t kGreyHex = 0x757575;
}  // namespace

// QR encoder (Nayuki qrcodegen) linked in through esp_emote_gfx, as in study_ui.cc.
extern "C" {
bool qrcodegen_encodeText(const char* text, uint8_t temp_buffer[], uint8_t qrcode[], int ecl,
                          int min_version, int max_version, int mask, bool boost_ecl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

void NavBadge::Update(const nav::Status& status) {
    std::string text;
    uint32_t color = kTealHex;
    if (status.module_seen && !status.has_fix) {
        text = "GPS: đang dò (" + std::to_string(status.satellites) + ")";
        color = kGreyHex;
    } else if (status.module_seen) {
        text = std::to_string(status.speed_kmh) + " km/h";
        if (status.limit_kmh > 0) {
            text += " / " + std::to_string(status.limit_kmh);
        }
        if (status.camera_m >= 0) {
            text += "  Camera " + std::to_string(status.camera_m) + " m";
            color = kOrangeHex;
        }
        if (status.overspeed) {
            color = kRedHex;
        }
    }
    if (text == shown_text_ && color == shown_color_) {
        return;
    }
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        return;
    }
    shown_text_ = text;
    shown_color_ = color;
    if (text.empty()) {
        if (label_ != nullptr) {
            lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
        return;
    }
    if (label_ == nullptr) {
        label_ = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_opa(label_, LV_OPA_80, 0);
        lv_obj_set_style_radius(label_, 10, 0);
        lv_obj_set_style_pad_hor(label_, 8, 0);
        lv_obj_set_style_pad_ver(label_, 2, 0);
        lv_obj_set_style_text_color(label_, lv_color_white(), 0);
        if (const lv_font_t* font = study_ui::TextFont()) {
            lv_obj_set_style_text_font(label_, font, 0);
        }
        lv_obj_remove_flag(label_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(label_, LV_ALIGN_TOP_LEFT, 6, 30);
    }
    lv_label_set_text(label_, text.c_str());
    lv_obj_set_style_bg_color(label_, lv_color_hex(color), 0);
    lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
    // Stay beneath full-screen overlays opened after the badge.
    lv_obj_move_to_index(label_, 0);
    lvgl_port_unlock();
}

bool NavSetupView::BuildQr(const std::string& text, int max_px) {
    constexpr int kMaxVersion = 10;
    constexpr size_t kBufferLen = ((kMaxVersion * 4 + 17) * (kMaxVersion * 4 + 17) + 7) / 8 + 1;
    uint8_t qrcode[kBufferLen];
    uint8_t temp[kBufferLen];
    if (!qrcodegen_encodeText(text.c_str(), temp, qrcode, /*ecl LOW*/ 0, 1, kMaxVersion, /*mask AUTO*/ -1, true)) {
        ESP_LOGW(TAG, "QR code too long: %s", text.c_str());
        return false;
    }
    const int modules = qrcodegen_getSize(qrcode);
    constexpr int kQuiet = 2;
    const int scale = std::max(1, max_px / (modules + 2 * kQuiet));
    const int side = (modules + 2 * kQuiet) * scale;
    qr_pixels_.assign(static_cast<size_t>(side) * side, 0xFFFF);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (!qrcodegen_getModule(qrcode, x, y)) {
                continue;
            }
            for (int dy = 0; dy < scale; ++dy) {
                uint16_t* row = &qr_pixels_[static_cast<size_t>((y + kQuiet) * scale + dy) * side];
                std::fill_n(row + (x + kQuiet) * scale, scale, 0x0000);
            }
        }
    }
    qr_dsc_ = {};
    qr_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    qr_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    qr_dsc_.header.w = side;
    qr_dsc_.header.h = side;
    qr_dsc_.header.stride = side * 2;
    qr_dsc_.data_size = qr_pixels_.size() * 2;
    qr_dsc_.data = reinterpret_cast<const uint8_t*>(qr_pixels_.data());
    return true;
}

void NavSetupView::Show() {
    // Starts the settings page, so do it before taking the LVGL lock.
    const std::string url = nav::SetupUrl();
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    handlers_.clear();
    panel_ = study_ui::CreateOverlay();
    lv_obj_set_flex_align(panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    study_ui::AddLabel(panel_, "Cài đặt trên xe: quét mã bằng điện thoại", kInk);
    if (url.empty()) {
        study_ui::AddLabel(panel_, "Robot chưa có Wi-Fi.", lv_color_hex(kRedHex));
    } else {
        if (BuildQr(url, 140)) {
            lv_obj_t* qr = lv_image_create(panel_);
            lv_image_set_src(qr, &qr_dsc_);
        }
        study_ui::AddLabel(panel_, url, kBlue);
        study_ui::AddLabel(panel_, "Nhập key TomTom, địa điểm, camera, giờ báo đường buổi sáng.", kInk);
    }
    lv_obj_t* close = study_ui::AddButton(panel_, "Đóng", kGrey, [this]() { Hide(); }, handlers_);
    lv_obj_set_size(close, LV_PCT(60), 36);
    shown_ = true;
    lvgl_port_unlock();
}

void NavSetupView::Hide() {
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
