#include "figure_view.h"

#include "study/physics_bank.h"
#include "study_ui.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#include <cstring>

#define TAG "FigureView"

namespace {
constexpr int kLockTimeoutMs = 3000;
constexpr int kButtonHeight = 36;
}  // namespace

void FigureView::DeleteObjects() {
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    image_data_.clear();
    handlers_.clear();
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

void FigureView::Show(const PhysicsFigure* figure, const std::string& caption, bool is_problem) {
    ESP_LOGI(TAG, "Show request: %s, figure %s", caption.c_str(), figure ? "yes" : "none");
    if (figure == nullptr) {
        Hide();
        return;
    }
    if (is_problem) {
        last_figure_ = figure;
        last_caption_ = caption;
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

    panel_ = study_ui::CreateOverlay();
    lv_obj_set_style_pad_all(panel_, 2, 0);
    lv_obj_set_style_pad_row(panel_, 2, 0);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    study_ui::OnClick(panel_, [this]() { Hide(); }, handlers_);

    lv_obj_t* title = study_ui::AddLabel(panel_, caption, lv_color_black());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    // The image scrolls by itself when it is taller than the space left for it.
    lv_obj_t* scroller = lv_obj_create(panel_);
    lv_obj_remove_style_all(scroller);
    lv_obj_set_width(scroller, LV_PCT(100));
    lv_obj_set_flex_grow(scroller, 1);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroller, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    study_ui::OnClick(scroller, [this]() { Hide(); }, handlers_);
    lv_obj_t* image = lv_image_create(scroller);
    lv_image_set_src(image, &image_);

    lv_obj_t* row = lv_obj_create(panel_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kButtonHeight);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto add = [&](const char* text, uint32_t color, std::function<void()> action) {
        lv_obj_t* button = study_ui::AddButton(row, text, lv_color_hex(color), std::move(action), handlers_);
        lv_obj_set_height(button, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
    };
    if (is_problem) {
        // Hint and solution come from the bank and show here; the next problem is up to the assistant.
        add("Gợi ý", 0xFB8C00, [this]() {
            if (actions_.hint) {
                actions_.hint();
            }
        });
        add("Lời giải", 0x1E88E5, [this]() {
            if (actions_.solution) {
                actions_.solution();
            }
        });
        add("Bài khác", 0x43A047, [this]() {
            if (actions_.next) {
                actions_.next();
            }
        });
    }
    add("Đóng", 0x757575, [this]() { Hide(); });

    shown_ = true;
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Showing figure %ux%u", figure->width, figure->height);
}

void FigureView::ShowNote(const std::string& title, const std::string& text) {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    DeleteObjects();
    BuildNote(title, text);
    shown_ = true;
    lvgl_port_unlock();
}

void FigureView::BuildNote(const std::string& title, const std::string& text) {
    panel_ = study_ui::CreateOverlay();
    lv_obj_set_style_pad_all(panel_, 4, 0);
    lv_obj_set_style_pad_row(panel_, 4, 0);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* heading = study_ui::AddLabel(panel_, title, lv_color_hex(0x1E88E5));
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);

    // Long solutions scroll; drag the text up and down.
    lv_obj_t* scroller = lv_obj_create(panel_);
    lv_obj_remove_style_all(scroller);
    lv_obj_set_width(scroller, LV_PCT(100));
    lv_obj_set_flex_grow(scroller, 1);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
    study_ui::AddLabel(scroller, text, lv_color_hex(0x202020));

    lv_obj_t* row = lv_obj_create(panel_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kButtonHeight);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto add = [&](const char* label, uint32_t color, std::function<void()> action) {
        lv_obj_t* button = study_ui::AddButton(row, label, lv_color_hex(color), std::move(action), handlers_);
        lv_obj_set_height(button, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
    };
    if (last_figure_ != nullptr) {
        add("Xem đề", 0x43A047, [this]() { Show(last_figure_, last_caption_, true); });
    }
    add("Đóng", 0x757575, [this]() { Hide(); });
}
