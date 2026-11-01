#ifndef FIGURE_VIEW_H
#define FIGURE_VIEW_H

#include <lvgl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct PhysicsFigure;

// Full-screen overlay that shows a physics problem's screen image (drawing and data summary)
// or a formula sheet above the normal UI. Tall images scroll; a tap outside the buttons closes
// it. The methods take the LVGL lock themselves, so call them from any task except the LVGL task.
class FigureView {
public:
    // Quick actions shown under a problem; each runs on the main task.
    struct ProblemActions {
        std::function<void()> hint;
        std::function<void()> solution;
        std::function<void()> next;
    };
    void SetProblemActions(ProblemActions actions) { actions_ = std::move(actions); }

    // Shows the image with a caption, replacing anything already shown. nullptr hides it.
    // Problems get the quick-action buttons; other images only a close button.
    void Show(const PhysicsFigure* figure, const std::string& caption, bool is_problem);
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    void DeleteObjects();

    ProblemActions actions_;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    lv_obj_t* panel_ = nullptr;
    lv_image_dsc_t image_ = {};
    std::vector<uint8_t> image_data_;  // palette + pixels, must outlive the lv_image
    std::atomic<bool> shown_{false};
};

#endif  // FIGURE_VIEW_H
