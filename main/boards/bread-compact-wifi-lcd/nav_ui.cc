#include "nav_ui.h"

#include "study_ui.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_lvgl_port.h>
#include <lvgl_private.h>  // image decoder descriptors, lv_image_cache_drop

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

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
        text = "GPS... " + std::to_string(status.satellites) + " sat";
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
        // LVGL's built-in font (ASCII text only): the badge can appear right after boot, and the
        // theme font is freed when the assets partition loads.
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

// ---------------------------------------------------------------- driving screen

namespace {
const lv_color_t kDark = lv_color_hex(0x111111);
const lv_color_t kWhite = lv_color_hex(0xFFFFFF);
const lv_color_t kSoft = lv_color_hex(0xEEEEEE);
const lv_color_t kOrange = lv_color_hex(kOrangeHex);
const lv_color_t kRed = lv_color_hex(kRedHex);
const lv_color_t kGreenText = lv_color_hex(0xC5F56A);
constexpr int kTile = 256;
constexpr int kMinZoom = 12;
constexpr int kMaxZoom = 18;
constexpr size_t kMaxTiles = 12;
constexpr double kPi = 3.14159265358979323846;

// Segments a..g: top, upper right, lower right, bottom, lower left, upper left, middle.
constexpr uint8_t kDigitSegments[11] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40,
};

uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Tile {
    std::vector<uint16_t> pixels;  // kTile * kTile, empty when the download failed
    int64_t failed_at = 0;
    uint32_t used = 0;
};

std::map<uint64_t, Tile> tiles_;  // only the map task touches it
uint32_t tile_clock_ = 0;

uint64_t TileKey(int z, int x, int y) {
    return (static_cast<uint64_t>(z) << 48) | (static_cast<uint64_t>(x & 0xFFFFFF) << 24) |
           static_cast<uint64_t>(y & 0xFFFFFF);
}

// Web Mercator world pixel of a point at `zoom`.
void WorldPixel(double lat, double lon, int zoom, double& x, double& y) {
    const double size = kTile * std::pow(2.0, zoom);
    const double lat_rad = lat * kPi / 180.0;
    x = (lon + 180.0) / 360.0 * size;
    y = (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / kPi) / 2.0 * size;
}

// Decodes a PNG with LVGL's decoder (LodePNG) into RGB565. Call with the LVGL lock held.
bool DecodePng(const std::string& png, std::vector<uint16_t>& out) {
    lv_image_dsc_t src = {};
    src.header.magic = LV_IMAGE_HEADER_MAGIC;
    src.header.cf = LV_COLOR_FORMAT_RAW;
    src.data_size = png.size();
    src.data = reinterpret_cast<const uint8_t*>(png.data());
    lv_image_decoder_args_t args = {};
    args.no_cache = true;
    lv_image_decoder_dsc_t dsc;
    if (lv_image_decoder_open(&dsc, &src, &args) != LV_RESULT_OK) {
        return false;
    }
    const lv_draw_buf_t* buffer = dsc.decoded;
    if (buffer == nullptr || buffer->data == nullptr) {
        lv_image_decoder_close(&dsc);
        return false;
    }
    const int w = std::min<int>(buffer->header.w, kTile);
    const int h = std::min<int>(buffer->header.h, kTile);
    const uint32_t stride = buffer->header.stride;
    const auto cf = static_cast<lv_color_format_t>(buffer->header.cf);
    out.assign(static_cast<size_t>(kTile) * kTile, Rgb565(0xE0, 0xE0, 0xE0));
    bool ok = true;
    for (int y = 0; y < h && ok; ++y) {
        const uint8_t* row = buffer->data + y * stride;
        uint16_t* dst = &out[static_cast<size_t>(y) * kTile];
        for (int x = 0; x < w; ++x) {
            switch (cf) {
                case LV_COLOR_FORMAT_ARGB8888:
                case LV_COLOR_FORMAT_XRGB8888:
                    dst[x] = Rgb565(row[x * 4 + 2], row[x * 4 + 1], row[x * 4]);
                    break;
                case LV_COLOR_FORMAT_RGB888:
                    dst[x] = Rgb565(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
                    break;
                case LV_COLOR_FORMAT_RGB565:
                    dst[x] = reinterpret_cast<const uint16_t*>(row)[x];
                    break;
                default:
                    ok = false;
                    break;
            }
            if (!ok) {
                break;
            }
        }
    }
    if (!ok) {
        ESP_LOGW(TAG, "Map tile color format %d not handled", static_cast<int>(cf));
    }
    lv_image_decoder_close(&dsc);
    return ok;
}

void FillCircle(std::vector<uint16_t>& pixels, int width, int height, int cx, int cy, int r, uint16_t color) {
    for (int y = std::max(0, cy - r); y <= std::min(height - 1, cy + r); ++y) {
        for (int x = std::max(0, cx - r); x <= std::min(width - 1, cx + r); ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                pixels[static_cast<size_t>(y) * width + x] = color;
            }
        }
    }
}

// Filled triangle (car arrow), by testing each pixel of its bounding box.
void FillTriangle(std::vector<uint16_t>& pixels, int width, int height, const float px[3], const float py[3],
                  uint16_t color) {
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({px[0], px[1], px[2]}))));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({px[0], px[1], px[2]}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({py[0], py[1], py[2]}))));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({py[0], py[1], py[2]}))));
    auto edge = [](float ax, float ay, float bx, float by, float x, float y) {
        return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    };
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float e0 = edge(px[0], py[0], px[1], py[1], x + 0.5f, y + 0.5f);
            const float e1 = edge(px[1], py[1], px[2], py[2], x + 0.5f, y + 0.5f);
            const float e2 = edge(px[2], py[2], px[0], py[0], x + 0.5f, y + 0.5f);
            if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                pixels[static_cast<size_t>(y) * width + x] = color;
            }
        }
    }
}

lv_obj_t* PlainBox(lv_obj_t* parent) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

lv_obj_t* TextLabel(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, color, 0);
    if (const lv_font_t* font = study_ui::TextFont()) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

const lv_color_t kCameraGreen = lv_color_hex(0x4CD964);

// Recolors the camera icon (its children 0, 1, 3 and 4 carry the colour).
void SetCameraIconColor(lv_obj_t* icon, lv_color_t color) {
    for (int index : {0, 1, 3, 4}) {
        lv_obj_set_style_bg_color(lv_obj_get_child(icon, index), color, 0);
    }
}

// A video camera drawn from boxes, 36 x 22: green normally, blinking red near a camera.
lv_obj_t* CameraIcon(lv_obj_t* parent, lv_color_t red, lv_color_t dark) {
    lv_obj_t* icon = PlainBox(parent);
    // Designed on a 60 x 36 grid, drawn at 3/5 size.
    auto px = [](int value) { return (value * 3 + 2) / 5; };
    lv_obj_set_size(icon, px(60), px(36));
    auto box = [&](int x, int y, int w, int h, lv_color_t color, int radius) {
        lv_obj_t* part = PlainBox(icon);
        lv_obj_set_pos(part, px(x), px(y));
        lv_obj_set_size(part, std::max(2, px(w)), std::max(2, px(h)));
        radius = radius == LV_RADIUS_CIRCLE ? radius : px(radius);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(part, color, 0);
        lv_obj_set_style_radius(part, radius, 0);
        return part;
    };
    box(8, 0, 14, 6, red, 2);                       // top grip
    box(2, 5, 40, 28, red, 5);                      // body
    box(13, 9, 20, 20, dark, LV_RADIUS_CIRCLE);     // lens ring
    box(18, 14, 10, 10, red, LV_RADIUS_CIRCLE);     // lens
    box(44, 11, 14, 16, red, 2);                    // front lens hood
    box(6, 9, 5, 5, lv_color_hex(0xFFFFFF), LV_RADIUS_CIRCLE);  // recording light
    return icon;
}

std::string GpsText(const nav::Status& status) {
    if (status.module_seen && status.has_fix) {
        return "GPS tốt: " + std::to_string(status.satellites) + " vệ tinh";
    }
    if (status.module_seen) {
        return "Đang tìm vệ tinh (" + std::to_string(status.satellites) + ")";
    }
    return status.has_fix ? std::string("Vị trí từ điện thoại") : std::string("Chưa có GPS");
}
}  // namespace

void SegmentDigit::Create(lv_obj_t* parent, int x, int y, int w, int h, int t, lv_color_t color) {
    const int half = h / 2;
    const int rects[7][4] = {
        {x + t, y, w - 2 * t, t},                     // a
        {x + w - t, y + t, t, half - t - t / 2},      // b
        {x + w - t, y + half + t / 2, t, half - t - t / 2},  // c
        {x + t, y + h - t, w - 2 * t, t},             // d
        {x, y + half + t / 2, t, half - t - t / 2},   // e
        {x, y + t, t, half - t - t / 2},              // f
        {x + t, y + half - t / 2, w - 2 * t, t},      // g
    };
    for (int i = 0; i < 7; ++i) {
        lv_obj_t* segment = PlainBox(parent);
        lv_obj_set_pos(segment, rects[i][0], rects[i][1]);
        lv_obj_set_size(segment, rects[i][2], rects[i][3]);
        lv_obj_set_style_radius(segment, t / 3, 0);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(segment, color, 0);
        segments_[i] = segment;
    }
}

void SegmentDigit::Set(int value) {
    const uint8_t mask = value >= 0 && value <= 10 ? kDigitSegments[value] : 0;
    for (int i = 0; i < 7; ++i) {
        if (segments_[i] == nullptr) {
            continue;
        }
        if (mask & (1 << i)) {
            lv_obj_remove_flag(segments_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(segments_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void SegmentDigit::SetColor(lv_color_t color) {
    for (lv_obj_t* segment : segments_) {
        if (segment != nullptr) {
            lv_obj_set_style_bg_color(segment, color, 0);
        }
    }
}

namespace {
// Shows `value` right-aligned on `digits` (count of them), dashes when value < 0.
void ShowNumber(SegmentDigit* digits, int count, int value) {
    for (int i = count - 1; i >= 0; --i) {
        if (value < 0) {
            digits[i].Set(i == count - 1 || i == count - 2 ? 10 : -1);
            continue;
        }
        const int place = count - 1 - i;
        int divisor = 1;
        for (int k = 0; k < place; ++k) {
            divisor *= 10;
        }
        const bool leading_blank = place > 0 && value < divisor;
        digits[i].Set(leading_blank ? -1 : (value / divisor) % 10);
    }
}
}  // namespace

void DriveView::Show(Page page) {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    Build(page);
    lvgl_port_unlock();
    if (page == Page::kMap) {
        map_dirty_ = true;
        map_active_ = true;
        if (map_task_ == nullptr) {
            TaskHandle_t handle = nullptr;
            xTaskCreate(MapTaskEntry, "nav_map", 8192, this, 2, &handle);
            map_task_ = handle;
        }
    }
}

void DriveView::Hide() {
    map_active_ = false;
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    StopBlinkLocked();
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    map_image_ = nullptr;
    handlers_.clear();
    shown_ = false;
    lvgl_port_unlock();
}

void DriveView::Build(Page page) {
    map_active_ = false;
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    map_image_ = nullptr;
    handlers_.clear();
    StopBlinkLocked();
    page_ = page;
    lv_display_t* display = lv_display_get_default();
    width_ = lv_display_get_horizontal_resolution(display);
    height_ = lv_display_get_vertical_resolution(display);

    panel_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel_);
    lv_obj_set_size(panel_, width_, height_);
    lv_obj_set_pos(panel_, 0, 0);
    lv_obj_set_style_bg_color(panel_, kDark, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICKABLE);
    study_ui::OnClick(
        panel_, [this]() { Show(page_ == Page::kSpeed ? Page::kMap : Page::kSpeed); }, handlers_);

    nav::Status status;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status = status_;
    }

    if (page == Page::kSpeed) {
        gps_label_ = TextLabel(panel_, kSoft);
        lv_obj_set_pos(gps_label_, 8, 8);
        lv_obj_set_width(gps_label_, width_ - 82);  // the camera icon sits at the top right
        lv_label_set_long_mode(gps_label_, LV_LABEL_LONG_MODE_DOTS);
        // Big speed: three seven-segment digits.
        const int dw = std::min(56, (width_ - 40) / 3);
        const int dh = dw * 2 - 10;
        const int gap = 10;
        const int x0 = (width_ - (3 * dw + 2 * gap)) / 2;
        for (int i = 0; i < 3; ++i) {
            speed_digits_[i].Create(panel_, x0 + i * (dw + gap), 44, dw, dh, dw / 5, kWhite);
        }
        lv_obj_t* unit = TextLabel(panel_, kSoft);
        lv_label_set_text(unit, "km/h");
        lv_obj_set_pos(unit, x0 + 3 * dw + 2 * gap - 40, 44 + dh + 6);

        // Speed limit sign.
        const int sign_y = 44 + dh + 34;
        const int sign = 84;
        lv_obj_t* circle = PlainBox(panel_);
        lv_obj_set_pos(circle, 10, sign_y);
        lv_obj_set_size(circle, sign, sign);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(circle, kWhite, 0);
        lv_obj_set_style_border_width(circle, 9, 0);
        lv_obj_set_style_border_color(circle, kRed, 0);
        const int lw = 16, lh = 30, lgap = 4;
        const int lx = 10 + (sign - (3 * lw + 2 * lgap)) / 2;
        for (int i = 0; i < 3; ++i) {
            limit_digits_[i].Create(panel_, lx + i * (lw + lgap), sign_y + (sign - lh) / 2, lw, lh, 4,
                                    lv_color_hex(0x000000));
        }

        alert_icon_ = CameraIcon(panel_, kCameraGreen, kDark);
        lv_obj_set_pos(alert_icon_, 10 + sign + 10, sign_y + 2);
        camera_label_ = TextLabel(panel_, kWhite);
        lv_obj_set_pos(camera_label_, 10 + sign + 10, sign_y + 28);
        lv_obj_set_width(camera_label_, width_ - sign - 30);
        lv_label_set_long_mode(camera_label_, LV_LABEL_LONG_WRAP);

        camera_bar_ = lv_bar_create(panel_);
        lv_obj_remove_flag(camera_bar_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(camera_bar_, 10, sign_y + sign + 10);
        lv_obj_set_size(camera_bar_, width_ - 20, 14);
        lv_bar_set_range(camera_bar_, 0, 700);
        lv_obj_set_style_bg_color(camera_bar_, lv_color_hex(0x5A3300), LV_PART_MAIN);
        lv_obj_set_style_bg_color(camera_bar_, kOrange, LV_PART_INDICATOR);

        note_label_ = TextLabel(panel_, kGreenText);
        lv_obj_set_width(note_label_, width_ - 16);
        lv_label_set_long_mode(note_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_align(note_label_, LV_ALIGN_BOTTOM_LEFT, 8, -6);
        UpdateSpeedPageLocked(status);
    } else {
        const size_t pixels = static_cast<size_t>(width_) * height_;
        if (map_front_.size() != pixels) {
            map_front_.assign(pixels, Rgb565(0x30, 0x30, 0x30));
        }
        map_dsc_ = {};
        map_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        map_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        map_dsc_.header.w = width_;
        map_dsc_.header.h = height_;
        map_dsc_.header.stride = width_ * 2;
        map_dsc_.data_size = pixels * 2;
        map_dsc_.data = reinterpret_cast<const uint8_t*>(map_front_.data());
        map_image_ = lv_image_create(panel_);
        lv_image_set_src(map_image_, &map_dsc_);
        lv_obj_remove_flag(map_image_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(map_image_, 0, 0);

        // Top row (left of the X button): speed or GPS state, limit sign, camera icon.
        lv_obj_t* row = PlainBox(panel_);
        lv_obj_set_pos(row, 6, 6);
        lv_obj_set_size(row, width_ - 56, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_style_pad_row(row, 4, 0);

        map_speed_ = TextLabel(row, kWhite);
        lv_obj_set_style_bg_opa(map_speed_, LV_OPA_80, 0);
        lv_obj_set_style_bg_color(map_speed_, kDark, 0);
        lv_obj_set_style_radius(map_speed_, 8, 0);
        lv_obj_set_style_pad_hor(map_speed_, 6, 0);
        lv_obj_set_style_pad_ver(map_speed_, 2, 0);

        map_limit_ = PlainBox(row);
        lv_obj_set_size(map_limit_, 32, 32);
        lv_obj_set_style_radius(map_limit_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(map_limit_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(map_limit_, kWhite, 0);
        lv_obj_set_style_border_width(map_limit_, 4, 0);
        lv_obj_set_style_border_color(map_limit_, kRed, 0);
        map_limit_label_ = lv_label_create(map_limit_);
        lv_obj_set_style_text_color(map_limit_label_, lv_color_hex(0x000000), 0);
        lv_obj_center(map_limit_label_);

        alert_icon_ = CameraIcon(row, kCameraGreen, kDark);

        map_info_ = TextLabel(panel_, kWhite);
        lv_obj_set_style_bg_opa(map_info_, LV_OPA_70, 0);
        lv_obj_set_style_bg_color(map_info_, kDark, 0);
        lv_obj_set_style_pad_hor(map_info_, 4, 0);
        lv_obj_set_width(map_info_, width_ - 52);
        lv_label_set_long_mode(map_info_, LV_LABEL_LONG_WRAP);
        lv_obj_align(map_info_, LV_ALIGN_BOTTOM_LEFT, 4, -4);

        lv_obj_t* plus = study_ui::AddButton(panel_, "+", lv_color_hex(0x1E88E5), [this]() {
            zoom_ = std::min(kMaxZoom, zoom_.load() + 1);
            map_dirty_ = true;
        }, handlers_);
        lv_obj_set_size(plus, 38, 38);
        lv_obj_align(plus, LV_ALIGN_BOTTOM_RIGHT, -4, -48);
        lv_obj_t* minus = study_ui::AddButton(panel_, "-", lv_color_hex(0x1E88E5), [this]() {
            zoom_ = std::max(kMinZoom, zoom_.load() - 1);
            map_dirty_ = true;
        }, handlers_);
        lv_obj_set_size(minus, 38, 38);
        lv_obj_align(minus, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
        UpdateMapLabelsLocked(status);
    }

    lv_obj_set_style_border_color(panel_, kRed, 0);
    lv_obj_set_style_border_width(panel_, 6, 0);
    lv_obj_set_style_border_opa(panel_, LV_OPA_TRANSP, 0);
    SetAlertLocked(status);

    lv_obj_t* close = study_ui::AddButton(panel_, "X", kGrey, [this]() { Hide(); }, handlers_);
    lv_obj_set_size(close, 40, 32);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -4, 4);
    shown_ = true;
}

void DriveView::UpdateSpeedPageLocked(const nav::Status& status) {
    lv_label_set_text(gps_label_, GpsText(status).c_str());
    const bool speed_known = status.module_seen && status.has_fix;
    ShowNumber(speed_digits_, 3, speed_known ? std::min(status.speed_kmh, 299) : -1);
    for (auto& digit : speed_digits_) {
        digit.SetColor(status.overspeed ? kRed : kWhite);
    }
    ShowNumber(limit_digits_, 3, status.limit_kmh > 0 ? status.limit_kmh : -1);
    if (status.camera_m >= 0) {
        lv_label_set_text(camera_label_, ("Phía trước có\ncamera " + std::to_string(status.camera_m) + " m").c_str());
        lv_obj_set_style_text_color(camera_label_, kOrange, 0);
        lv_bar_set_value(camera_bar_, 700 - std::min(status.camera_m, 700), LV_ANIM_OFF);
        lv_obj_remove_flag(camera_bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(camera_label_, "Không có\ncamera gần");
        lv_obj_set_style_text_color(camera_label_, kSoft, 0);
        lv_obj_add_flag(camera_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    std::string note = status.note;
    if (note.empty() && !status.road.empty()) {
        note = "Đường " + status.road;
    }
    if (note.empty()) {
        note = status.module_seen ? "Chạm màn hình để xem bản đồ" : "Chưa cắm GPS (TX vào GPIO 5, RX vào GPIO 4) · chạm để xem bản đồ";
    }
    lv_label_set_text(note_label_, note.c_str());
}

void DriveView::UpdateMapLabelsLocked(const nav::Status& status) {
    std::string speed = status.module_seen && status.has_fix ? std::to_string(status.speed_kmh) + " km/h" : GpsText(status);
    if (status.camera_m >= 0) {
        speed += "  Camera " + std::to_string(status.camera_m) + " m";
    }
    lv_label_set_text(map_speed_, speed.c_str());
    if (status.limit_kmh > 0) {
        lv_label_set_text(map_limit_label_, std::to_string(status.limit_kmh).c_str());
        lv_obj_center(map_limit_label_);
        lv_obj_remove_flag(map_limit_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(map_limit_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(map_speed_, status.overspeed ? kRed : (status.camera_m >= 0 ? kOrange : kDark), 0);
    lv_label_set_text(map_info_, (map_message_.empty() ? std::string("Đang tải bản đồ...") : map_message_).c_str());
}

void DriveView::Update(const nav::Status& status) {
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = status;
    }
    if (!shown_) {
        return;
    }
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        return;
    }
    if (panel_ != nullptr) {
        if (page_ == Page::kSpeed) {
            UpdateSpeedPageLocked(status);
        } else {
            UpdateMapLabelsLocked(status);
        }
        SetAlertLocked(status);
    }
    lvgl_port_unlock();
}

void DriveView::StopBlinkLocked() {
    if (blink_timer_ != nullptr) {
        lv_timer_delete(blink_timer_);
        blink_timer_ = nullptr;
    }
    alert_icon_ = nullptr;
    camera_alert_ = false;
    speed_alert_ = false;
    blink_on_ = false;
}

void DriveView::SetAlertLocked(const nav::Status& status) {
    if (panel_ == nullptr || alert_icon_ == nullptr) {
        return;
    }
    // Same distance as the spoken camera alert.
    camera_alert_ = status.camera_m >= 0 && status.camera_m <= 500;
    speed_alert_ = status.overspeed;
    if ((camera_alert_ || speed_alert_) && blink_timer_ == nullptr) {
        blink_on_ = false;
        blink_timer_ = lv_timer_create(BlinkTimer, 400, this);
        BlinkTimer(blink_timer_);
    } else if (!camera_alert_ && !speed_alert_ && blink_timer_ != nullptr) {
        lv_timer_delete(blink_timer_);
        blink_timer_ = nullptr;
        blink_on_ = false;
        SetCameraIconColor(alert_icon_, kCameraGreen);
        lv_obj_remove_flag(alert_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_opa(panel_, LV_OPA_TRANSP, 0);
    }
}

// Runs in the LVGL task (lock held).
void DriveView::BlinkTimer(lv_timer_t* timer) {
    auto* self = static_cast<DriveView*>(lv_timer_get_user_data(timer));
    if (self->panel_ == nullptr || self->alert_icon_ == nullptr) {
        return;
    }
    self->blink_on_ = !self->blink_on_;
    if (self->camera_alert_) {
        // Red on, off, red on...
        SetCameraIconColor(self->alert_icon_, kRed);
        if (self->blink_on_) {
            lv_obj_remove_flag(self->alert_icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(self->alert_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        SetCameraIconColor(self->alert_icon_, kCameraGreen);
        lv_obj_remove_flag(self->alert_icon_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_border_opa(self->panel_, self->blink_on_ ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void DriveView::MapTaskEntry(void* arg) { static_cast<DriveView*>(arg)->MapTask(); }

void DriveView::MapTask() {
    int64_t last_us = 0;
    while (true) {
        if (!map_active_) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        const int64_t now = esp_timer_get_time();
        // Redraw every 2 s while driving (the car moves), at once after a zoom or page change.
        if (map_dirty_ || now - last_us > 2000 * 1000) {
            map_dirty_ = false;
            last_us = now;
            RenderMap();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void DriveView::RenderMap() {
    const int width = width_;
    const int height = height_;
    const int zoom = zoom_.load();
    double lat = 0, lon = 0;
    std::string label;
    nav::MapCenter(lat, lon, label);
    nav::Status status;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status = status_;
    }

    double cx = 0, cy = 0;
    WorldPixel(lat, lon, zoom, cx, cy);
    const double left = cx - width / 2.0;
    const double top = cy - height / 2.0;
    const int tx0 = static_cast<int>(std::floor(left / kTile));
    const int ty0 = static_cast<int>(std::floor(top / kTile));
    const int tx1 = static_cast<int>(std::floor((left + width - 1) / kTile));
    const int ty1 = static_cast<int>(std::floor((top + height - 1) / kTile));
    const bool has_key = nav::HasKey();
    ++tile_clock_;

    // Tiles on screen, the one under the car first so the useful part shows up soonest.
    struct Needed {
        int x, y;
        double distance;
    };
    std::vector<Needed> needed;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const double dx = (tx + 0.5) * kTile - cx;
            const double dy = (ty + 0.5) * kTile - cy;
            needed.push_back({tx, ty, dx * dx + dy * dy});
            tiles_[TileKey(zoom, tx, ty)].used = tile_clock_;
        }
    }
    std::sort(needed.begin(), needed.end(), [](const Needed& a, const Needed& b) { return a.distance < b.distance; });

    const double meters_per_pixel = 156543.03 * std::cos(lat * kPi / 180.0) / std::pow(2.0, zoom);
    const auto cameras = nav::CamerasNear(lat, lon, meters_per_pixel * std::hypot(width, height) / 2.0);
    const bool gps = status.module_seen && status.has_fix;

    // Draws the tiles we have, the cameras and the car, then shows it with `message`.
    auto present = [&](const std::string& message) {
        map_back_.assign(static_cast<size_t>(width) * height, Rgb565(0x30, 0x30, 0x30));
        for (const auto& n : needed) {
            const Tile& tile = tiles_[TileKey(zoom, n.x, n.y)];
            if (tile.pixels.empty()) {
                continue;
            }
            const int ox = static_cast<int>(std::floor(n.x * static_cast<double>(kTile) - left));
            const int oy = static_cast<int>(std::floor(n.y * static_cast<double>(kTile) - top));
            const int sx0 = std::max(0, ox);
            const int sx1 = std::min(width, ox + kTile);
            if (sx1 <= sx0) {
                continue;
            }
            for (int y = std::max(0, oy); y < std::min(height, oy + kTile); ++y) {
                memcpy(&map_back_[static_cast<size_t>(y) * width + sx0],
                       &tile.pixels[static_cast<size_t>(y - oy) * kTile + (sx0 - ox)], (sx1 - sx0) * 2);
            }
        }
        for (const auto& camera : cameras) {
            double px = 0, py = 0;
            WorldPixel(camera.first, camera.second, zoom, px, py);
            const int sx = static_cast<int>(px - left);
            const int sy = static_cast<int>(py - top);
            FillCircle(map_back_, width, height, sx, sy, 7, Rgb565(0xFF, 0xFF, 0xFF));
            FillCircle(map_back_, width, height, sx, sy, 5, Rgb565(0xFB, 0x8C, 0x00));
        }
        const int mx = width / 2;
        const int my = height / 2;
        if (gps && status.speed_kmh >= 5) {
            // Arrow along the course: tip 16 px ahead, two back corners, white outline.
            const float a = status.course_deg * static_cast<float>(kPi) / 180.0f;
            const float sa = std::sin(a), ca = std::cos(a);
            const float local[3][2] = {{0, -16}, {-10, 10}, {10, 10}};
            float px[3], py[3], ox[3], oy[3];
            for (int i = 0; i < 3; ++i) {
                px[i] = mx + local[i][0] * ca - local[i][1] * sa;
                py[i] = my + local[i][0] * sa + local[i][1] * ca;
                ox[i] = mx + (px[i] - mx) * 1.3f;
                oy[i] = my + (py[i] - my) * 1.3f;
            }
            FillTriangle(map_back_, width, height, ox, oy, Rgb565(0xFF, 0xFF, 0xFF));
            FillTriangle(map_back_, width, height, px, py, Rgb565(0x1E, 0x88, 0xE5));
        } else {
            FillCircle(map_back_, width, height, mx, my, 9, Rgb565(0xFF, 0xFF, 0xFF));
            FillCircle(map_back_, width, height, mx, my, 6, gps ? Rgb565(0x1E, 0x88, 0xE5) : Rgb565(0xE5, 0x39, 0x35));
        }
        if (!lvgl_port_lock(kLockTimeoutMs)) {
            return;
        }
        if (map_active_ && map_image_ != nullptr && map_front_.size() == map_back_.size()) {
            memcpy(map_front_.data(), map_back_.data(), map_back_.size() * 2);
            lv_image_cache_drop(&map_dsc_);
            lv_obj_invalidate(map_image_);
            map_message_ = message;
            UpdateMapLabelsLocked(status);
        }
        lvgl_port_unlock();
    };
    // Stop as soon as the user zooms or leaves the map; the next round starts over.
    auto aborted = [&]() { return !map_active_ || zoom_.load() != zoom; };

    present("Đang tải bản đồ...");
    int missing = 0;
    int index = 0;
    for (const auto& n : needed) {
        ++index;
        Tile& tile = tiles_[TileKey(zoom, n.x, n.y)];
        const int64_t now = esp_timer_get_time();
        if (!tile.pixels.empty()) {
            continue;
        }
        if (!has_key || (tile.failed_at != 0 && now - tile.failed_at < 15 * 1000 * 1000)) {
            ++missing;
            continue;
        }
        std::string png;
        bool ok = nav::FetchMapTile(zoom, n.x, n.y, png);
        const int64_t fetched = esp_timer_get_time();
        bool decoded = false;
        if (ok && lvgl_port_lock(kLockTimeoutMs)) {
            decoded = DecodePng(png, tile.pixels);
            lvgl_port_unlock();
        }
        ESP_LOGI(TAG, "Map tile %d/%d/%d: download %s (%u bytes, %d ms), decode %s (%d ms)", zoom, n.x, n.y,
                 ok ? "ok" : "failed", static_cast<unsigned>(png.size()), static_cast<int>((fetched - now) / 1000),
                 decoded ? "ok" : "failed", static_cast<int>((esp_timer_get_time() - fetched) / 1000));
        if (!(ok && decoded)) {
            tile.pixels.clear();
            tile.failed_at = now;
            ++missing;
        }
        if (aborted()) {
            map_dirty_ = true;
            return;
        }
        if (index < static_cast<int>(needed.size())) {
            present("Đang tải bản đồ " + std::to_string(index) + "/" + std::to_string(needed.size()) + "...");
        }
    }

    // Forget the tiles used least recently.
    while (tiles_.size() > kMaxTiles) {
        auto oldest = tiles_.begin();
        for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
            if (it->second.used < oldest->second.used) {
                oldest = it;
            }
        }
        tiles_.erase(oldest);
    }

    std::string message;
    if (!has_key) {
        message = "Cần key TomTom để tải bản đồ (nói: cài đặt dẫn đường)";
    } else if (missing > 0) {
        message = "Chưa tải được " + std::to_string(missing) + " ô bản đồ, đang thử lại...";
    } else if (label == "GPS") {
        message = "Zoom " + std::to_string(zoom) + " · chạm để về màn tốc độ";
    } else {
        message = "Chưa có GPS: quanh " + label + " · zoom " + std::to_string(zoom);
    }
    present(message);
}

void DriveButton::Create(std::function<void()> on_click) {
    lv_obj_t* button = study_ui::AddButton(lv_layer_top(), LV_SYMBOL_GPS " Xe", lv_color_hex(kTealHex),
                                           std::move(on_click), handlers_);
    lv_obj_set_size(button, kWidth, kHeight);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
    // LVGL's built-in font, like the Menu button: the theme font is swapped after boot.
    lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, kMargin, -kMargin);
    lv_obj_move_to_index(button, 0);
    button_ = button;
}

void DriveButton::SetHidden(bool hidden) {
    if (button_ == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(button_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(button_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool DriveButton::Contains(int x, int y, int screen_width, int screen_height) {
    (void)screen_width;
    constexpr int kSlack = 8;
    return x <= kWidth + kMargin + kSlack && y >= screen_height - kHeight - kMargin - kSlack;
}
