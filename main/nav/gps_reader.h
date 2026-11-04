#pragma once

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <mutex>
#include <string>

// Reads a NEO-6M style GPS module (NMEA 0183 at 9600 baud) on a spare UART and keeps the last fix.
class GpsReader {
public:
    struct Fix {
        bool valid = false;        // the module has a position (RMC status A)
        double lat = 0;            // degrees, south negative
        double lon = 0;            // degrees, west negative
        float speed_kmh = 0;
        float course_deg = 0;      // 0 = north, clockwise; only meaningful when moving
        int satellites = 0;        // from GGA
        int64_t updated_us = 0;    // esp_timer time of the last valid RMC sentence
    };

    static GpsReader& GetInstance() {
        static GpsReader instance;
        return instance;
    }

    // Starts the UART and the reader task. `rx` is the robot pin wired to the module's TX.
    bool Start(gpio_num_t rx, gpio_num_t tx, int baud = 9600);
    Fix GetFix() const;
    // True when NMEA sentences arrived in the last 5 s (module wired and powered).
    bool ModuleSeen() const;
    // Fix younger than 5 s.
    bool HasFreshFix() const;

    // Overrides the position with one the phone sent (no module, or no signal yet). Used until
    // the module has its own fix.
    void SetPhoneFix(double lat, double lon);

private:
    GpsReader() = default;
    static void TaskEntry(void* arg);
    void Run();
    void HandleSentence(const std::string& line);

    mutable std::mutex mutex_;
    Fix fix_;
    Fix phone_fix_;
    int64_t last_sentence_us_ = 0;
    TaskHandle_t task_ = nullptr;
};
