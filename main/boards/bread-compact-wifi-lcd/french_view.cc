#include "french_view.h"

#if CONFIG_XIAOZHI_FRENCH_EDITION
#include "application.h"
#include "study_ui.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>

#define TAG "FrenchView"

namespace {
constexpr int kLockTimeoutMs = 3000;
constexpr int kButtonHeight = 36;
constexpr uint32_t kBlue = 0x1E88E5;
constexpr uint32_t kGreen = 0x43A047;
constexpr uint32_t kRed = 0xE53935;
constexpr uint32_t kOrange = 0xFB8C00;
constexpr uint32_t kPurple = 0x8E24AA;
constexpr uint32_t kGrey = 0x757575;
}  // namespace

void FrenchView::DeleteObjects() {
    if (panel_ != nullptr) {
        lv_obj_delete(panel_);
        panel_ = nullptr;
    }
    handlers_.clear();
    shown_ = false;
}

void FrenchView::Hide() {
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    DeleteObjects();
    lvgl_port_unlock();
}

lv_obj_t* FrenchView::AddButtonRow(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kButtonHeight);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

void FrenchView::AddRowButton(lv_obj_t* row, const char* text, uint32_t color, std::function<void()> action) {
    lv_obj_t* button = study_ui::AddButton(row, text, lv_color_hex(color), std::move(action), handlers_);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_flex_grow(button, 1);
}

void FrenchView::ShowLesson(int lesson) {
    auto& course = FrenchCourse::GetInstance();
    const std::string title = course.ScreenTitle(lesson);
    const std::string text = course.ScreenText(lesson);
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    DeleteObjects();
    panel_ = study_ui::CreateOverlay();
    lv_obj_set_style_pad_all(panel_, 4, 0);
    lv_obj_set_style_pad_row(panel_, 4, 0);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* heading = study_ui::AddLabel(panel_, title, lv_color_hex(kBlue));
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);

    // The lesson is longer than the screen: drag it up and down.
    lv_obj_t* scroller = lv_obj_create(panel_);
    lv_obj_remove_style_all(scroller);
    lv_obj_set_width(scroller, LV_PCT(100));
    lv_obj_set_flex_grow(scroller, 1);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
    study_ui::AddLabel(scroller, text, lv_color_hex(0x202020));

    lv_obj_t* row = AddButtonRow(panel_);
    AddRowButton(row, "<", kGrey, [this, lesson]() { FrenchCourse::GetInstance().ShowLesson(lesson - 1); });
    AddRowButton(row, "Học", kGreen, [this, lesson]() {
        Hide();
        Application::GetInstance().AskAssistant("Dạy mình bài tiếng Pháp số " + std::to_string(lesson + 1));
    });
    AddRowButton(row, "Ôn từ", kOrange, [this]() { FrenchCourse::GetInstance().ShowQuiz(); });
    AddRowButton(row, ">", kGrey, [this, lesson]() { FrenchCourse::GetInstance().ShowLesson(lesson + 1); });
    AddRowButton(row, "X", kGrey, [this]() { Hide(); });
    shown_ = true;
    lvgl_port_unlock();
}

void FrenchView::ShowQuiz(const FrenchCourse::Quiz& quiz) {
    quiz_ = quiz;
    if (!lvgl_port_lock(kLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    BuildQuiz(false);
    lvgl_port_unlock();
}

void FrenchView::BuildQuiz(bool reveal) {
    DeleteObjects();
    auto& course = FrenchCourse::GetInstance();
    panel_ = study_ui::CreateOverlay();
    lv_obj_set_style_pad_all(panel_, 6, 0);
    lv_obj_set_style_pad_row(panel_, 8, 0);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    const std::string heading = std::string("Ôn từ vựng - bài ") + std::to_string(quiz_.lesson + 1) +
                                (course.WeakCount() > 0 ? ", cần ôn " + std::to_string(course.WeakCount()) : "");
    study_ui::AddLabel(panel_, heading, lv_color_hex(kBlue));
    study_ui::AddLabel(panel_, quiz_.fr_to_vi ? "Nghĩa tiếng Việt là gì?" : "Tiếng Pháp nói thế nào?",
                       lv_color_hex(0x606060));

    lv_obj_t* question = study_ui::AddLabel(panel_, quiz_.question, lv_color_hex(0x202020));
    lv_obj_set_style_text_align(question, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(question, lv_color_hex(0xFFF3E0), 0);
    lv_obj_set_style_bg_opa(question, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(question, 8, 0);
    lv_obj_set_style_pad_all(question, 12, 0);

    lv_obj_t* answer = study_ui::AddLabel(panel_, reveal ? quiz_.answer : std::string("?"), lv_color_hex(kGreen));
    lv_obj_set_style_text_align(answer, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* spacer = lv_obj_create(panel_);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    // The French word, for the assistant to read aloud.
    const std::string french = quiz_.fr_to_vi ? quiz_.question : quiz_.answer;
    lv_obj_t* row = AddButtonRow(panel_);
    if (!reveal) {
        AddRowButton(row, "Xem đáp án", kBlue, [this]() {
            if (lvgl_port_lock(kLockTimeoutMs)) {
                BuildQuiz(true);
                lvgl_port_unlock();
            }
        });
    } else {
        AddRowButton(row, "Đúng", kGreen, [this]() {
            FrenchCourse::GetInstance().RecordQuiz(quiz_.lesson, quiz_.word, true);
            FrenchCourse::GetInstance().ShowQuiz();
        });
        AddRowButton(row, "Sai", kRed, [this]() {
            FrenchCourse::GetInstance().RecordQuiz(quiz_.lesson, quiz_.word, false);
            FrenchCourse::GetInstance().ShowQuiz();
        });
    }
    lv_obj_t* row2 = AddButtonRow(panel_);
    AddRowButton(row2, "Nghe đọc", kPurple, [french]() {
        Application::GetInstance().AskAssistant("Đọc mẫu chậm từ tiếng Pháp này rồi cho mình đọc theo: " + french);
    });
    AddRowButton(row2, "Bài học", kOrange, [this]() {
        FrenchCourse::GetInstance().ShowLesson(quiz_.lesson);
    });
    AddRowButton(row2, "X", kGrey, [this]() { Hide(); });
    shown_ = true;
}
#endif  // CONFIG_XIAOZHI_FRENCH_EDITION
