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
    pin_a_ = rx;
    pin_b_ = tx;
    baud_ = baud;
    uart_config_t config = {};
    config.baud_rate = baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    // The robot never talks to the module, so no TX pin is driven: while the reader searches it
    // may listen on either wire, and driving the module's own TX line would fight it.
    if (uart_driver_install(kUart, 1024, 0, 0, nullptr, 0) != ESP_OK ||
        uart_param_config(kUart, &config) != ESP_OK ||
        uart_set_pin(kUart, UART_PIN_NO_CHANGE, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot open the GPS UART");
        return false;
    }
    // Without a module the pins float; the pull-up keeps them from reading noise as data.
    gpio_set_pull_mode(rx, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(tx, GPIO_PULLUP_ONLY);
    xTaskCreate(TaskEntry, "gps", 3072, this, 3, &task_);
    ESP_LOGI(kTag, "GPS reader on RX GPIO %d (also tries GPIO %d), %d baud", rx, tx, baud);
    return true;
}

void GpsReader::TaskEntry(void* arg) { static_cast<GpsReader*>(arg)->Run(); }

void GpsReader::Listen(gpio_num_t rx, int baud) {
    uart_set_pin(kUart, UART_PIN_NO_CHANGE, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_baudrate(kUart, baud);
    uart_flush_input(kUart);
}

void GpsReader::Run() {
    // GY-NEO6MV2 ships at 9600 baud, but clones and modules reconfigured by u-center use other
    // rates, and TX/RX are easy to swap. Until a sentence with a good checksum arrives, the reader
    // cycles through both wires and the common rates, 3 s each, and logs what it hears.
    static const int kBauds[] = {9600, 38400, 4800, 115200, 57600, 19200};
    const gpio_num_t pins[] = {pin_a_, pin_b_};
    std::vector<int> bauds = {baud_};
    for (int b : kBauds) {
        if (b != baud_) {
            bauds.push_back(b);
        }
    }
    const size_t tries = bauds.size() * 2;
    size_t attempt = 0;
    gpio_num_t rx = pin_a_;
    int baud = baud_;
    bool locked = false;
    int64_t attempt_start = esp_timer_get_time();
    int bytes_this_try = 0;
    int good_this_try = 0;
    int bad_this_try = 0;
    std::string sample;
    int64_t last_report = attempt_start;
    int bytes_since_report = 0;
    int good_since_report = 0;

    std::string line;
    uint8_t buffer[128];
    while (true) {
        const int read = uart_read_bytes(kUart, buffer, sizeof(buffer), pdMS_TO_TICKS(500));
        if (read > 0) {
            bytes_this_try += read;
            bytes_since_report += read;
            std::lock_guard<std::mutex> lock(mutex_);
            last_byte_us_ = esp_timer_get_time();
        }
        for (int i = 0; i < read; ++i) {
            const char c = static_cast<char>(buffer[i]);
            if (sample.size() < 40) {
                sample += (c >= 32 && c < 127) ? c : '.';
            }
            if (c == '$') {
                line.assign(1, c);
            } else if (c == '\r' || c == '\n') {
                if (line.size() > 6) {
                    if (HandleSentence(line)) {
                        ++good_this_try;
                        ++good_since_report;
                    } else {
                        ++bad_this_try;
                    }
                }
                line.clear();
            } else if (!line.empty() && line.size() < 120) {
                line += c;
            }
        }

        const int64_t now = esp_timer_get_time();
        if (!locked) {
            if (good_this_try >= 2) {
                locked = true;
                ESP_LOGI(kTag, "GPS module found: module TX on GPIO %d, %d baud", rx, baud);
                if (rx != pin_a_) {
                    ESP_LOGW(kTag, "TX/RX wires are swapped (works anyway; module TX belongs on GPIO %d)", pin_a_);
                }
            } else if (now - attempt_start > 3LL * 1000 * 1000) {
                if (bytes_this_try > 0) {
                    ESP_LOGW(kTag, "GPIO %d @ %d baud: %d bytes, %d bad lines, no valid NMEA: \"%s\"", rx, baud,
                             bytes_this_try, bad_this_try, sample.c_str());
                }
                ++attempt;
                if (attempt % tries == 0) {
                    ESP_LOGW(kTag, "No GPS data on GPIO %d or %d at any baud rate: check VCC/GND and the TX wire",
                             pin_a_, pin_b_);
                }
                rx = pins[attempt % 2];
                baud = bauds[(attempt / 2) % bauds.size()];
                Listen(rx, baud);
                line.clear();
                sample.clear();
                attempt_start = now;
                bytes_this_try = good_this_try = bad_this_try = 0;
            }
        } else if (now - last_report > 30LL * 1000 * 1000) {
            Fix fix = GetFix();
            ESP_LOGI(kTag, "GPS: %d bytes, %d sentences in 30 s, fix %s, %d satellites", bytes_since_report,
                     good_since_report, HasFreshFix() ? "yes" : "no", fix.satellites);
            if (bytes_since_report == 0) {
                // Wire came loose or the module was swapped: search again.
                locked = false;
                attempt_start = now;
                bytes_this_try = good_this_try = bad_this_try = 0;
                sample.clear();
            }
            last_report = now;
            bytes_since_report = good_since_report = 0;
        }
    }
}

bool GpsReader::HandleSentence(const std::string& line) {
    if (!ChecksumOk(line)) {
        return false;
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
            return true;
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
    return true;
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

bool GpsReader::BytesSeen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_byte_us_ != 0 && esp_timer_get_time() - last_byte_us_ <= kFreshUs;
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
