#include "app_launcher.h"

#include "app_icons.h"
#include "study_ui.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#include <algorithm>

#define TAG "AppLauncher"

namespace {
constexpr int kLockTimeoutMs = 3000;
constexpr int kHeaderHeight = 30;
constexpr int kColumns = 3;
constexpr int kRows = 4;
constexpr int kTile = 50;
const lv_color_t kBg = lv_color_hex(0x0B1020);
const lv_color_t kText = lv_color_hex(0xFFFFFF);
const lv_color_t kSub = lv_color_hex(0xC8D6E5);
const lv_color_t kDotOff = lv_color_hex(0x33405A);
}  // namespace

lv_obj_t* CreateAppTile(lv_obj_t* parent, int x, int y, int tile, int cell_w, const AppEntry& app,
                        const std::string& label, std::vector<std::unique_ptr<std::function<void()>>>& handlers) {
    // A transparent cell covers the tile and its label so the whole area is tappable.
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, cell_w, label.empty() ? tile : tile + 22);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* square = lv_obj_create(cell);
    lv_obj_remove_style_all(square);
    lv_obj_remove_flag(square, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(square, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(square, tile, tile);
    lv_obj_align(square, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(square, tile / 4, 0);
    lv_obj_set_style_bg_color(square, lv_color_hex(app.color), 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
    // Pressed feedback: the tile darkens while the finger is on it.
    lv_obj_set_style_bg_color(square, lv_color_darken(lv_color_hex(app.color), LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_add_flag(square, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* icon = lv_label_create(square);
    lv_obj_set_style_text_font(icon, &font_app_icons_30, 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_label_set_text(icon, app.icon);
    lv_obj_center(icon);

    if (!label.empty()) {
        lv_obj_t* text = lv_label_create(cell);
        lv_obj_set_width(text, cell_w);
        lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(text, kSub, 0);
        lv_label_set_text(text, label.c_str());
        lv_obj_align(text, LV_ALIGN_TOP_MID, 0, tile + 3);
    }
    study_ui::OnClick(cell, app.action, handlers);
    return cell;
}

void AppLauncher::Hide() {
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

void AppLauncher::Show(int page) {
    page_ = std::clamp(page, 0, std::max(0, static_cast<int>(pages_.size()) - 1));
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    Build();
    lvgl_port_unlock();
}

void AppLauncher::ChangePage(int delta) {
    if (!shown_ || pages_.empty()) {
        return;
    }
    const int page = std::clamp(page_ + delta, 0, static_cast<int>(pages_.size()) - 1);
    if (page != page_) {
        page_ = page;
        Build();
    }
}

void AppLauncher::Build() {
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    handlers_.clear();
    if (pages_.empty()) {
        return;
    }
    const Page& page = pages_[page_];

    panel_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel_);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(panel_, kBg, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    if (const lv_font_t* font = study_ui::TextFont()) {
        lv_obj_set_style_text_font(panel_, font, 0);
    }
    const int width = lv_obj_get_width(lv_layer_top());
    const int height = lv_obj_get_height(lv_layer_top());

    lv_obj_t* title = lv_label_create(panel_);
    lv_obj_set_style_text_color(title, kText, 0);
    lv_label_set_text(title, page.title.c_str());
    lv_obj_set_pos(title, 8, 6);

    // Page dots, then a close button in the top-right corner.
    const int pages = static_cast<int>(pages_.size());
    for (int i = 0; i < pages; ++i) {
        lv_obj_t* dot = lv_obj_create(panel_);
        lv_obj_remove_style_all(dot);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(dot, i == page_ ? 14 : 6, 6);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, i == page_ ? kText : kDotOff, 0);
        lv_obj_set_pos(dot, width - 56 - (pages - i) * 18 + (i == page_ ? 0 : 4), 13);
    }
    lv_obj_t* close = study_ui::AddButton(panel_, "X", lv_color_hex(0x33405A), [this]() { Hide(); }, handlers_);
    lv_obj_set_size(close, 36, 26);
    lv_obj_set_pos(close, width - 40, 2);

    const int cell_w = width / kColumns;
    const int row_h = (height - kHeaderHeight) / kRows;
    for (size_t i = 0; i < page.apps.size() && i < static_cast<size_t>(kColumns * kRows); ++i) {
        const AppEntry& app = page.apps[i];
        const int col = static_cast<int>(i) % kColumns;
        const int row = static_cast<int>(i) / kColumns;
        AppEntry entry = app;
        // Close the grid first; the app then opens its own screen.
        entry.action = [this, action = app.action]() {
            Hide();
            if (action) {
                action();
            }
        };
        CreateAppTile(panel_, col * cell_w, kHeaderHeight + row * row_h + 4, kTile, cell_w, entry,
                      app.label ? app.label() : std::string(), handlers_);
    }
    if (page_ + 1 < pages) {
        lv_obj_t* hint = lv_label_create(panel_);
        lv_obj_set_style_text_color(hint, kDotOff, 0);
        lv_obj_set_style_text_font(hint, LV_FONT_DEFAULT, 0);
        lv_label_set_text(hint, LV_SYMBOL_RIGHT);
        lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -2, 10);
    }
    shown_ = true;
}
