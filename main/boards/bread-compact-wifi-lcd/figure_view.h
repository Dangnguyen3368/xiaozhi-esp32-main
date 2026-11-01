#ifndef FIGURE_VIEW_H
#define FIGURE_VIEW_H

#include <lvgl.h>

#include <atomic>
#include <string>
#include <vector>

struct PhysicsFigure;

// Full-screen overlay that shows a physics problem's line drawing above the normal UI.
// The methods take the LVGL lock themselves, so call them from any task except the LVGL task.
class FigureView {
public:
    // Shows the figure with a caption, replacing any figure already shown. nullptr hides it.
    void Show(const PhysicsFigure* figure, const std::string& caption);
    void Hide();
    bool IsShown() const { return shown_.load(); }

private:
    void DeleteObjects();

    lv_obj_t* panel_ = nullptr;
    lv_image_dsc_t image_ = {};
    std::vector<uint8_t> image_data_;  // palette + pixels, must outlive the lv_image
    std::atomic<bool> shown_{false};
};

#endif  // FIGURE_VIEW_H
