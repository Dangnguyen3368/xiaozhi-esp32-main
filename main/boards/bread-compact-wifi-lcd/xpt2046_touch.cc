#include "xpt2046_touch.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>

#define TAG "Xpt2046Touch"

namespace {
// 12-bit differential conversions; power-down bits 00 keep PENIRQ enabled between reads.
constexpr uint8_t kReadX = 0xD0;
constexpr uint8_t kReadY = 0x90;
constexpr uint8_t kReadZ1 = 0xB0;
constexpr int kSamples = 5;
// Idle panels read z1 of 0-2; even a light press at a corner gives z1 well above 100.
constexpr int kMinZ1 = 30;
constexpr int64_t kMaxTapUs = 800 * 1000;
// A resistive panel briefly loses contact during one press; only count a release after this
// many idle polls, and ignore taps that follow the previous one too closely.
constexpr int kReleasePolls = 3;
constexpr int64_t kMinTapGapUs = 400 * 1000;
}  // namespace

Xpt2046Touch::Xpt2046Touch(spi_host_device_t host, gpio_num_t cs, gpio_num_t irq, int width,
                           int height, const Calibration& calibration)
    : irq_(irq), width_(width), height_(height), calibration_(calibration) {
    spi_device_interface_config_t dev_config = {};
    dev_config.clock_speed_hz = 2 * 1000 * 1000;
    dev_config.mode = 0;
    dev_config.spics_io_num = cs;
    dev_config.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(host, &dev_config, &device_));

    if (irq_ != GPIO_NUM_NC) {
        gpio_config_t io_config = {};
        io_config.pin_bit_mask = 1ULL << irq_;
        io_config.mode = GPIO_MODE_INPUT;
        io_config.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK(gpio_config(&io_config));
    }
    ESP_LOGI(TAG, "XPT2046 ready (CS %d, IRQ %d)", cs, irq);
}

uint16_t Xpt2046Touch::Transfer(uint8_t command) {
    spi_transaction_t transaction = {};
    transaction.length = 24;
    transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    transaction.tx_data[0] = command;
    if (spi_device_polling_transmit(device_, &transaction) != ESP_OK) {
        return 0;
    }
    return ((transaction.rx_data[1] << 8) | transaction.rx_data[2]) >> 3;
}

bool Xpt2046Touch::ReadRaw(int& raw_x, int& raw_y) {
    // Sample PENIRQ first, then convert every poll: PENIRQ only works after the controller
    // has received a command with power-down bits 00, so never skip the transfers.
    const int irq_level = irq_ != GPIO_NUM_NC ? gpio_get_level(irq_) : 0;
    const int z1 = Transfer(kReadZ1);
    if (irq_level != 0) {
        return false;
    }
    if (z1 < kMinZ1) {
        return false;
    }

    // Median of a few samples filters the jitter typical of resistive panels.
    int xs[kSamples];
    int ys[kSamples];
    for (int i = 0; i < kSamples; ++i) {
        xs[i] = Transfer(kReadX);
        ys[i] = Transfer(kReadY);
    }
    std::sort(xs, xs + kSamples);
    std::sort(ys, ys + kSamples);
    raw_x = xs[kSamples / 2];
    raw_y = ys[kSamples / 2];
    // A reading pinned at either rail means the pen lifted mid-conversion. Corners legitimately
    // read close to the rails, so only reject the extremes.
    return raw_x > 10 && raw_x < 4085 && raw_y > 10 && raw_y < 4085;
}

void Xpt2046Touch::MapToScreen(int raw_x, int raw_y, int& x, int& y) const {
    int a = raw_x;
    int b = raw_y;
    if (calibration_.swap_xy) {
        std::swap(a, b);
    }
    x = (a - calibration_.raw_x_min) * (width_ - 1) / (calibration_.raw_x_max - calibration_.raw_x_min);
    y = (b - calibration_.raw_y_min) * (height_ - 1) / (calibration_.raw_y_max - calibration_.raw_y_min);
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    if (calibration_.mirror_x) {
        x = width_ - 1 - x;
    }
    if (calibration_.mirror_y) {
        y = height_ - 1 - y;
    }
}

void Xpt2046Touch::ReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<Xpt2046Touch*>(lv_indev_get_user_data(indev));
    int raw_x = 0;
    int raw_y = 0;
    const bool touched = self->ReadRaw(raw_x, raw_y);
    if (touched) {
        self->MapToScreen(raw_x, raw_y, self->last_x_, self->last_y_);
        self->last_raw_x_ = raw_x;
        self->last_raw_y_ = raw_y;
        if (!self->pressed_) {
            self->press_start_us_ = esp_timer_get_time();
        }
        self->pressed_ = true;
        self->idle_polls_ = 0;
    } else if (self->pressed_ && ++self->idle_polls_ >= kReleasePolls) {
        self->pressed_ = false;
        const int64_t now = esp_timer_get_time();
        ESP_LOGI(TAG, "Touch raw x=%d y=%d -> screen x=%d y=%d", self->last_raw_x_,
                 self->last_raw_y_, self->last_x_, self->last_y_);
        if (self->on_tap_ && now - self->press_start_us_ < kMaxTapUs &&
            now - self->last_tap_us_ > kMinTapGapUs) {
            self->last_tap_us_ = now;
            self->on_tap_(self->last_x_, self->last_y_);
        }
    }
    data->point.x = self->last_x_;
    data->point.y = self->last_y_;
    data->state = self->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void Xpt2046Touch::AttachToLvgl() {
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, ReadCallback);
    lv_indev_set_user_data(indev, this);
    lv_indev_set_display(indev, lv_display_get_default());
    ESP_LOGI(TAG, "Touch input attached to LVGL");
}
