#include "study_ui.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "lvgl_font.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#include <algorithm>
#include <cstdio>

#define TAG "StudyUi"

namespace {
constexpr int kLockTimeoutMs = 3000;
const lv_color_t kInk = lv_color_hex(0x202020);
const lv_color_t kBlue = lv_color_hex(0x1E88E5);
const lv_color_t kGreen = lv_color_hex(0x43A047);
const lv_color_t kOrange = lv_color_hex(0xFB8C00);
const lv_color_t kPurple = lv_color_hex(0x8E24AA);
const lv_color_t kTeal = lv_color_hex(0x00897B);
const lv_color_t kRed = lv_color_hex(0xE53935);
const lv_color_t kGrey = lv_color_hex(0x757575);

void RunOnMainTask(lv_event_t* e) {
    auto* action = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
    if (action != nullptr && *action) {
        // Copy: the overlay (and this handler) may be deleted before the task runs.
        std::function<void()> copy = *action;
        Application::GetInstance().Schedule(std::move(copy));
    }
}

// A row that lays its children out side by side, wrapping onto a new line when full.
lv_obj_t* AddButtonGrid(lv_obj_t* parent) {
    lv_obj_t* grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    return grid;
}
}  // namespace

namespace study_ui {

const lv_font_t* TextFont() {
    if (auto* display = Board::GetInstance().GetDisplay(); display && display->GetTheme()) {
        if (auto text_font = display->GetTheme()->GetTextFont()) {
            return text_font->font();
        }
    }
    return nullptr;
}

lv_obj_t* CreateOverlay() {
    lv_obj_t* panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    if (const lv_font_t* font = TextFont()) {
        lv_obj_set_style_text_font(panel, font, 0);
    }
    return panel;
}

lv_obj_t* AddLabel(lv_obj_t* parent, const std::string& text, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text.c_str());
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* AddButton(lv_obj_t* parent, const std::string& text, lv_color_t color, std::function<void()> action,
                    std::vector<std::unique_ptr<std::function<void()>>>& handlers) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text.c_str());
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    OnClick(button, std::move(action), handlers);
    return button;
}

void OnClick(lv_obj_t* obj, std::function<void()> action,
             std::vector<std::unique_ptr<std::function<void()>>>& handlers) {
    handlers.push_back(std::make_unique<std::function<void()>>(std::move(action)));
    lv_obj_add_event_cb(obj, RunOnMainTask, LV_EVENT_CLICKED, handlers.back().get());
}

}  // namespace study_ui

void StudyMenu::Hide() {
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

void StudyMenu::Show() { Build(Page::kMain); }

void StudyMenu::ShowFormulaTopics() { Build(Page::kFormulas); }

void StudyMenu::ShowComputer() { Build(Page::kComputer); }

void StudyMenu::BuildComputer(lv_obj_t* grid,
                              const std::function<void(const char*, lv_color_t, std::function<void()>)>& add) {
    (void)grid;
    auto command = [this](const char* action, const char* text) {
        return [this, action, text]() {
            if (actions_.pc_command) {
                actions_.pc_command(action, text);
            }
        };
    };
    add("Đọc để gõ", kBlue, [this]() {
        Hide();
        if (actions_.start_dictation) {
            actions_.start_dictation();
        }
    });
    add("Chụp đề (đ.thoại)", kGreen, [this]() { Build(Page::kPhoto); });
    add("Mở Word", kBlue, command("open", "word"));
    add("Mở Excel", kGreen, command("open", "excel"));
    add("Mở Notepad", kTeal, command("open", "notepad"));
    add("Xuống dòng", kPurple, command("key", "enter"));
    add("Tab (ô bên)", kPurple, command("key", "tab"));
    add("Xóa lùi", kOrange, command("key", "backspace"));
    add("Hoàn tác", kOrange, command("key", "ctrl+z"));
    add("Lưu (Ctrl+S)", kGreen, command("key", "ctrl+s"));
    add("Đóng cửa sổ", kRed, command("key", "alt+f4"));
    add("Ghép máy", kTeal, [this]() {
        pair_code_.clear();
        Build(Page::kPair);
    });
    add("Quay lại", kGrey, [this]() { Show(); });
}

void StudyMenu::BuildPair(const std::function<void(const char*, lv_color_t, std::function<void()>)>& add) {
    static const char* const kDigits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (const char* digit : kDigits) {
        add(digit, kBlue, [this, digit]() {
            if (pair_code_.size() < 4) {
                pair_code_ += digit;
            }
            Build(Page::kPair);
        });
    }
    add("Xóa", kOrange, [this]() {
        if (!pair_code_.empty()) {
            pair_code_.pop_back();
        }
        Build(Page::kPair);
    });
    add("0", kBlue, [this]() {
        if (pair_code_.size() < 4) {
            pair_code_ += "0";
        }
        Build(Page::kPair);
    });
    add("Ghép", kGreen, [this]() {
        if (pair_code_.size() != 4) {
            return;
        }
        if (actions_.pc_command) {
            actions_.pc_command("pair", pair_code_);
        }
        pair_code_.clear();
        ShowComputer();
    });
    add("Quay lại", kGrey, [this]() { ShowComputer(); });
}

// QR encoder (Nayuki qrcodegen) already linked in through esp_emote_gfx. LVGL's own copy
// (LV_USE_QRCODE) cannot be enabled next to it: both define the same symbols.
extern "C" {
bool qrcodegen_encodeText(const char* text, uint8_t temp_buffer[], uint8_t qrcode[], int ecl,
                          int min_version, int max_version, int mask, bool boost_ecl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

bool StudyMenu::BuildQrImage(const std::string& text, int max_px) {
    constexpr int kMaxVersion = 10;
    constexpr size_t kBufferLen = ((kMaxVersion * 4 + 17) * (kMaxVersion * 4 + 17) + 7) / 8 + 1;
    uint8_t qrcode[kBufferLen];
    uint8_t temp[kBufferLen];
    if (!qrcodegen_encodeText(text.c_str(), temp, qrcode, /*ecl LOW*/ 0, 1, kMaxVersion,
                              /*mask AUTO*/ -1, true)) {
        ESP_LOGW(TAG, "QR code too long: %s", text.c_str());
        return false;
    }
    const int modules = qrcodegen_getSize(qrcode);
    constexpr int kQuiet = 2;  // white border, in modules
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

void StudyMenu::Build(Page page) {
    const bool formula_topics = page == Page::kFormulas;
    // Read the toggle states before taking the LVGL lock.
    const bool paired = page == Page::kComputer && actions_.pc_paired && actions_.pc_paired();
    const bool pomodoro = actions_.pomodoro_running && actions_.pomodoro_running();
    const bool radio = actions_.radio_running && actions_.radio_running();
    const bool local_music = actions_.local_music_running && actions_.local_music_running();
    // Starts the robot's photo page, so do it before taking the LVGL lock too.
    const std::string photo_url =
        page == Page::kPhoto && actions_.phone_photo_url ? actions_.phone_photo_url() : std::string();

    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    handlers_.clear();

    using study_ui::AddButton;
    panel_ = study_ui::CreateOverlay();
    const char* title = "Menu học tập";
    if (formula_topics) {
        title = "Thẻ công thức";
    } else if (page == Page::kComputer) {
        title = paired ? "Máy tính (đã ghép)" : "Máy tính (chưa ghép: bấm Ghép máy)";
    } else if (page == Page::kPair) {
        title = "Nhập mã 4 số trên máy tính";
    } else if (page == Page::kPhoto) {
        title = "Điện thoại quét mã, chụp đề rồi gửi";
    }
    study_ui::AddLabel(panel_, title, kInk);
    if (page == Page::kPhoto) {
        if (photo_url.empty()) {
            study_ui::AddLabel(panel_, "Robot chưa có Wi-Fi.", kRed);
        } else {
            if (BuildQrImage(photo_url, 140)) {
                lv_obj_set_flex_align(panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_t* qr = lv_image_create(panel_);
                lv_image_set_src(qr, &qr_dsc_);
            }
            study_ui::AddLabel(panel_, photo_url, kBlue);
            study_ui::AddLabel(panel_, "Gửi xong robot tự giải và hiện lời giải.", kInk);
        }
    }
    if (page == Page::kPair) {
        std::string shown = pair_code_;
        while (shown.size() < 4) {
            shown += "_";
        }
        std::string spaced;
        for (char c : shown) {
            spaced += c;
            spaced += ' ';
        }
        study_ui::AddLabel(panel_, "Mã: " + spaced, kBlue);
    }
    lv_obj_t* grid = AddButtonGrid(panel_);
    const int width_pct = page == Page::kPair ? 31 : 48;
    std::function<void(const char*, lv_color_t, std::function<void()>)> add =
        [&](const char* text, lv_color_t color, std::function<void()> action) {
            lv_obj_t* button = AddButton(grid, text, color, std::move(action), handlers_);
            lv_obj_set_size(button, LV_PCT(width_pct), 36);
            if (lv_obj_t* label = lv_obj_get_child(button, 0)) {
                lv_obj_set_width(label, LV_PCT(100));
                lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            }
        };

    if (page == Page::kComputer) {
        BuildComputer(grid, add);
        shown_ = true;
        lvgl_port_unlock();
        return;
    }
    if (page == Page::kPhoto) {
        add("Quay lại", kGrey, [this]() { ShowComputer(); });
        shown_ = true;
        lvgl_port_unlock();
        return;
    }
    if (page == Page::kPair) {
        BuildPair(add);
        shown_ = true;
        lvgl_port_unlock();
        return;
    }
    if (formula_topics) {
        static const char* const kTopics[][2] = {
            {"Cơ học", "co"}, {"Nhiệt học", "nhiet"}, {"Điện học", "dien"}, {"Quang học", "quang"}};
        for (const auto& topic : kTopics) {
            const std::string key = topic[1];
            add(topic[0], kTeal, [this, key]() {
                Hide();
                if (actions_.formulas) {
                    actions_.formulas(key);
                }
            });
        }
        add("Quay lại", kGrey, [this]() { Show(); });
    } else {
        add("Luyện thi", kBlue, [this]() {
            Hide();
            if (actions_.practice) {
                actions_.practice();
            }
        });
        add("Ôn bài sai", kOrange, [this]() {
            Hide();
            if (actions_.review) {
                actions_.review();
            }
        });
        add(pomodoro ? "Dừng hẹn giờ" : "Hẹn giờ học", kPurple, [this]() {
            Hide();
            if (actions_.toggle_pomodoro) {
                actions_.toggle_pomodoro();
            }
        });
        add("Công thức", kTeal, [this]() { ShowFormulaTopics(); });
        add("Tiến độ", kGreen, [this]() {
            Hide();
            if (actions_.progress) {
                actions_.progress();
            }
        });
        add(radio ? "Tắt nhạc" : "Mở nhạc", kRed, [this]() {
            Hide();
            if (actions_.toggle_radio) {
                actions_.toggle_radio();
            }
        });
        add(local_music ? "Bài tiếp" : "Nhạc máy tính", kOrange, [this]() {
            Hide();
            if (actions_.local_music) {
                actions_.local_music();
            }
        });
        add("Máy tính", kPurple, [this]() { ShowComputer(); });
        add("Wi-Fi", kTeal, [this]() {
            Hide();
            if (actions_.wifi_setup) {
                actions_.wifi_setup();
            }
        });
        add("Cập nhật", kBlue, [this]() {
            Hide();
            if (actions_.update_firmware) {
                actions_.update_firmware();
            }
        });
    }
    add("Đóng", kGrey, [this]() { Hide(); });

    shown_ = true;
    lvgl_port_unlock();
}

void ProgressView::Hide() {
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

void ProgressView::Show(const PhysicsTutor::ProgressReport& report) {
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
    lv_obj_set_style_pad_row(panel_, 3, 0);
    study_ui::OnClick(panel_, [this]() { Hide(); }, handlers_);
    study_ui::AddLabel(panel_, "Tiến độ: " + report.learner, kInk);

    static const uint32_t kTopicColors[] = {0x1E88E5, 0xE53935, 0xFB8C00, 0x8E24AA, 0x00897B};
    char line[96];
    for (size_t t = 0; t < report.topics.size(); ++t) {
        const auto& topic = report.topics[t];
        if (topic.need_review > 0) {
            snprintf(line, sizeof(line), "%s  %d/%d  (cần ôn %d)", topic.name, topic.done, topic.total,
                     topic.need_review);
        } else {
            snprintf(line, sizeof(line), "%s  %d/%d", topic.name, topic.done, topic.total);
        }
        study_ui::AddLabel(panel_, line, kInk);
        lv_obj_t* bar = lv_bar_create(panel_);
        lv_obj_set_size(bar, LV_PCT(100), 12);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, topic.total > 0 ? topic.done * 100 / topic.total : 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kTopicColors[t % 5]), LV_PART_INDICATOR);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }
    snprintf(line, sizeof(line), "Tổng %d/%d bài · cần ôn %d · đến hạn hôm nay %d", report.done, report.total,
             report.need_review, report.due_today);
    study_ui::AddLabel(panel_, line, kGrey);
    lv_obj_t* close = study_ui::AddButton(panel_, "Đóng", kGrey, [this]() { Hide(); }, handlers_);
    lv_obj_set_size(close, LV_PCT(100), 40);

    shown_ = true;
    lvgl_port_unlock();
}

void PomodoroBadge::Update(const StudyTimer::State& state) {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        return;
    }
    if (!state.running) {
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
        lv_obj_align(label_, LV_ALIGN_TOP_MID, 0, 30);
    }
    char text[32];
    snprintf(text, sizeof(text), "%s %02d:%02d", state.on_break ? "Nghỉ" : "Học", state.remaining_seconds / 60,
             state.remaining_seconds % 60);
    lv_label_set_text(label_, text);
    lv_obj_set_style_bg_color(label_, state.on_break ? kGreen : kPurple, 0);
    lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
    // Stay beneath full-screen overlays (figures, menu) that were opened after the badge.
    lv_obj_move_to_index(label_, 0);
    lvgl_port_unlock();
}

void MenuButton::Create(std::function<void()> on_click) {
    lv_obj_t* button = study_ui::AddButton(lv_layer_top(), "Menu", kBlue, std::move(on_click), handlers_);
    lv_obj_set_size(button, kWidth, kHeight);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
    // Keep LVGL's built-in font: the theme font is replaced once the assets partition loads
    // after boot, which would leave this long-lived button pointing at a freed font.
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -kMargin, -kMargin);
    // Overlays opened later sit above it, so it never covers their buttons.
    lv_obj_move_to_index(button, 0);
}

bool MenuButton::Contains(int x, int y, int screen_width, int screen_height) {
    constexpr int kSlack = 8;  // resistive touch is off by a few pixels
    return x >= screen_width - kWidth - kMargin - kSlack && y >= screen_height - kHeight - kMargin - kSlack;
}
