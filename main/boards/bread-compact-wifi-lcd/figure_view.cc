#include "figure_view.h"

#include "board.h"
#include "display.h"
#include "lvgl_font.h"
#include "study/physics_bank.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#include <cstring>

#define TAG "FigureView"

namespace {
constexpr int kLockTimeoutMs = 3000;
constexpr int kMaxHeightWithHint = 240;
}  // namespace

void FigureView::DeleteObjects() {
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    image_data_.clear();
    shown_ = false;
}

void FigureView::Hide() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    DeleteObjects();
    lvgl_port_unlock();
}

void FigureView::Show(const PhysicsFigure* figure, const std::string& caption) {
    ESP_LOGI(TAG, "Show request: %s, figure %s", caption.c_str(), figure ? "yes" : "none");
    if (figure == nullptr) {
        Hide();
        return;
    }

    // LVGL I1 image: two ARGB8888 palette entries (0 = paper, 1 = ink), then the same
    // MSB-first rows the bank stores.
    const uint32_t stride = (figure->width + 7) / 8;
    const size_t pixels = stride * figure->height;
    std::vector<uint8_t> data(8 + pixels);
    const lv_color32_t palette[2] = {{.blue = 0xff, .green = 0xff, .red = 0xff, .alpha = 0xff},
                                     {.blue = 0x20, .green = 0x20, .red = 0x20, .alpha = 0xff}};
    memcpy(data.data(), palette, sizeof(palette));
    memcpy(data.data() + 8, figure->bits, pixels);

    const lv_font_t* font = nullptr;
    if (auto* display = Board::GetInstance().GetDisplay(); display && display->GetTheme()) {
        if (auto text_font = display->GetTheme()->GetTextFont()) {
            font = text_font->font();
        }
    }

    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    DeleteObjects();
    image_data_ = std::move(data);
    image_ = {};
    image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_.header.cf = LV_COLOR_FORMAT_I1;
    image_.header.w = figure->width;
    image_.header.h = figure->height;
    image_.header.stride = stride;
    image_.data_size = image_data_.size();
    image_.data = image_data_.data();

    panel_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(panel_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 0, 0);
    lv_obj_set_style_radius(panel_, 0, 0);
    lv_obj_set_style_pad_all(panel_, 2, 0);
    lv_obj_set_style_pad_row(panel_, 2, 0);
    lv_obj_set_flex_flow(panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel_);
    lv_label_set_text(title, caption.c_str());
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    if (font != nullptr) {
        lv_obj_set_style_text_font(title, font, 0);
    }

    lv_obj_t* image = lv_image_create(panel_);
    lv_image_set_src(image, &image_);

    // Figures with their data lines printed underneath are nearly screen-tall; skip the hint then.
    if (figure->height <= kMaxHeightWithHint) {
        lv_obj_t* hint = lv_label_create(panel_);
        lv_label_set_text(hint, "Chạm màn hình để đóng hình");
        lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(hint, font, 0);
        }
    }

    shown_ = true;
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Showing figure %ux%u", figure->width, figure->height);
}
