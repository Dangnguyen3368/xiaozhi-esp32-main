#ifndef XPT2046_TOUCH_H
#define XPT2046_TOUCH_H

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <lvgl.h>

#include <functional>

// Minimal XPT2046 resistive touch driver. It shares the SPI bus with the LCD and feeds
// an LVGL pointer input device, so LVGL widgets can react to touches.
class Xpt2046Touch {
public:
    struct Calibration {
        int raw_x_min, raw_x_max, raw_y_min, raw_y_max;
        bool swap_xy, mirror_x, mirror_y;
    };

    Xpt2046Touch(spi_host_device_t host, gpio_num_t cs, gpio_num_t irq, int width, int height,
                 const Calibration& calibration);

    // Registers the LVGL input device on the default display. Call after the display exists.
    void AttachToLvgl();

    // Called (from the LVGL task) for a short tap that no widget handled specially.
    void OnTap(std::function<void(int x, int y)> callback) { on_tap_ = std::move(callback); }

private:
    static void ReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    bool ReadRaw(int& raw_x, int& raw_y);
    uint16_t Transfer(uint8_t command);
    void MapToScreen(int raw_x, int raw_y, int& x, int& y) const;

    spi_device_handle_t device_ = nullptr;
    gpio_num_t irq_;
    int width_;
    int height_;
    Calibration calibration_;
    std::function<void(int, int)> on_tap_;

    bool pressed_ = false;
    int last_x_ = 0;
    int last_y_ = 0;
    int last_raw_x_ = 0;
    int last_raw_y_ = 0;
    int64_t press_start_us_ = 0;
    int64_t last_tap_us_ = 0;
    int idle_polls_ = 0;
};

#endif  // XPT2046_TOUCH_H
