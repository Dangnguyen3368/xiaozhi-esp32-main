#ifndef FRENCH_VIEW_H
#define FRENCH_VIEW_H

#include "sdkconfig.h"

#if CONFIG_XIAOZHI_FRENCH_EDITION
#include "french/french_course.h"

#include <lvgl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Screens of the French course (French tutor edition): a lesson card with its words, phrases
// and grammar, and vocabulary flash cards. The methods take the LVGL lock themselves, so call
// them from any task except the LVGL task.
class FrenchView {
public:
    void ShowLesson(int lesson);
    void ShowQuiz(const FrenchCourse::Quiz& quiz);
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    void DeleteObjects();
    void BuildQuiz(bool reveal);
    lv_obj_t* AddButtonRow(lv_obj_t* parent);
    void AddRowButton(lv_obj_t* row, const char* text, uint32_t color, std::function<void()> action);

    FrenchCourse::Quiz quiz_;
    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::atomic<bool> shown_{false};
};
#endif  // CONFIG_XIAOZHI_FRENCH_EDITION

#endif  // FRENCH_VIEW_H
