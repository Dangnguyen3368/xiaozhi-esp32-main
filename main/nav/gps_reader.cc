#include "gps_reader.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr char kTag[] = "GpsReader";
constexpr uart_port_t kUart = UART_NUM_1;
constexpr int64_t kFreshUs = 5LL * 1000 * 1000;
constexpr int64_t kPhoneFreshUs = 10LL * 60 * 1000 * 1000;  // the phone sends rarely

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t comma = text.find(',', start);
        fields.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) {
            return fields;
        }
        start = comma + 1;
    }
}

// "ddmm.mmmm" / "dddmm.mmmm" + hemisphere -> degrees.
double ParseCoordinate(const std::string& value, const std::string& hemisphere) {
    if (value.empty()) {
        return 0;
    }
    const double raw = strtod(value.c_str(), nullptr);
    const int degrees = static_cast<int>(raw / 100);
    double result = degrees + (raw - degrees * 100) / 60.0;
    if (hemisphere == "S" || hemisphere == "W") {
        result = -result;
    }
    return result;
}

bool ChecksumOk(const std::string& line) {
    const size_t star = line.rfind('*');
    if (line.size() < 4 || line[0] != '$' || star == std::string::npos || star + 3 > line.size()) {
        return false;
    }
    uint8_t sum = 0;
    for (size_t i = 1; i < star; ++i) {
        sum ^= static_cast<uint8_t>(line[i]);
    }
    return sum == static_cast<uint8_t>(strtol(line.substr(star + 1, 2).c_str(), nullptr, 16));
}
}  // namespace

bool GpsReader::Start(gpio_num_t rx, gpio_num_t tx, int baud) {
    if (task_ != nullptr) {
        return true;
    }
    uart_config_t config = {};
    config.baud_rate = baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(kUart, 1024, 0, 0, nullptr, 0) != ESP_OK ||
        uart_param_config(kUart, &config) != ESP_OK ||
        uart_set_pin(kUart, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot open the GPS UART");
        return false;
    }
    // Without a module the pin floats; the pull-up keeps it from reading noise as data.
    gpio_set_pull_mode(rx, GPIO_PULLUP_ONLY);
    xTaskCreate(TaskEntry, "gps", 3072, this, 3, &task_);
    ESP_LOGI(kTag, "GPS reader on RX GPIO %d, TX GPIO %d, %d baud", rx, tx, baud);
    return true;
}

void GpsReader::TaskEntry(void* arg) { static_cast<GpsReader*>(arg)->Run(); }

void GpsReader::Run() {
    std::string line;
    uint8_t buffer[128];
    while (true) {
        const int read = uart_read_bytes(kUart, buffer, sizeof(buffer), pdMS_TO_TICKS(1000));
        for (int i = 0; i < read; ++i) {
            const char c = static_cast<char>(buffer[i]);
            if (c == '$') {
                line.assign(1, c);
            } else if (c == '\r' || c == '\n') {
                if (line.size() > 6) {
                    HandleSentence(line);
                }
                line.clear();
            } else if (!line.empty() && line.size() < 120) {
                line += c;
            }
        }
    }
}

void GpsReader::HandleSentence(const std::string& line) {
    if (!ChecksumOk(line)) {
        return;
    }
    const std::string body = line.substr(1, line.rfind('*') - 1);
    const auto fields = Split(body);
    // Talker can be GP, GN, GL...; only the sentence type matters.
    const std::string type = fields[0].size() >= 5 ? fields[0].substr(2) : std::string();
    const int64_t now = esp_timer_get_time();
    std::lock_guard<std::mutex> lock(mutex_);
    last_sentence_us_ = now;
    if (type == "RMC" && fields.size() >= 9) {
        // $GPRMC,time,status,lat,N,lon,E,speed(knots),course,date,...
        if (fields[2] != "A") {
            fix_.valid = false;
            return;
        }
        fix_.valid = true;
        fix_.lat = ParseCoordinate(fields[3], fields[4]);
        fix_.lon = ParseCoordinate(fields[5], fields[6]);
        fix_.speed_kmh = strtof(fields[7].c_str(), nullptr) * 1.852f;
        if (!fields[8].empty()) {
            fix_.course_deg = strtof(fields[8].c_str(), nullptr);
        }
        fix_.updated_us = now;
    } else if (type == "GGA" && fields.size() >= 8) {
        fix_.satellites = atoi(fields[7].c_str());
    }
}

GpsReader::Fix GpsReader::GetFix() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = esp_timer_get_time();
    if (fix_.valid && now - fix_.updated_us <= kFreshUs) {
        return fix_;
    }
    if (phone_fix_.valid && now - phone_fix_.updated_us <= kPhoneFreshUs) {
        Fix fix = phone_fix_;
        fix.satellites = fix_.satellites;
        return fix;
    }
    Fix stale = fix_;
    stale.valid = false;
    return stale;
}

bool GpsReader::ModuleSeen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_sentence_us_ != 0 && esp_timer_get_time() - last_sentence_us_ <= kFreshUs;
}

bool GpsReader::HasFreshFix() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fix_.valid && esp_timer_get_time() - fix_.updated_us <= kFreshUs;
}

void GpsReader::SetPhoneFix(double lat, double lon) {
    std::lock_guard<std::mutex> lock(mutex_);
    phone_fix_.valid = true;
    phone_fix_.lat = lat;
    phone_fix_.lon = lon;
    phone_fix_.speed_kmh = 0;
    phone_fix_.updated_us = esp_timer_get_time();
}
