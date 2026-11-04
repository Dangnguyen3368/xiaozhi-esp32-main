#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#include <lvgl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// One app: an icon tile with a label. `action` runs on the main task.
struct AppEntry {
    const char* icon;     // a glyph from app_icons.h
    uint32_t color;       // tile colour, 0xRRGGBB
    std::function<std::string()> label;  // read when the grid is built (toggles change it)
    std::function<void()> action;
};

// Draws one rounded app tile (icon centred, label underneath when `label` is not empty) and
// makes a click on it run `action` on the main task. Call with the LVGL lock held.
lv_obj_t* CreateAppTile(lv_obj_t* parent, int x, int y, int tile, int cell_w, const AppEntry& app,
                        const std::string& label, std::vector<std::unique_ptr<std::function<void()>>>& handlers);

// Full-screen grid of app icons, 3 x 4 per page, swipe left/right between pages. Opened by
// the Menu button, a swipe up or a long press, and by the "Ứng dụng" icon of the home screen.
// Show()/Hide() take the LVGL lock (call from the main task); ChangePage() runs in the LVGL
// task (from the touch swipe callback).
class AppLauncher {
public:
    struct Page {
        std::string title;
        std::vector<AppEntry> apps;
    };

    void SetPages(std::vector<Page> pages) { pages_ = std::move(pages); }
    void Show(int page = 0);
    void Hide();
    bool IsShown() const { return shown_.load(); }
    // Swipe left (+1) / right (-1). Call in the LVGL task with the lock held.
    void ChangePage(int delta);

private:
    void Build();

    std::vector<Page> pages_;
    int page_ = 0;
    lv_obj_t* panel_ = nullptr;
    std::vector<std::unique_ptr<std::function<void()>>> handlers_;
    std::atomic<bool> shown_{false};
};

#endif  // APP_LAUNCHER_H
