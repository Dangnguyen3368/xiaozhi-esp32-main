#ifndef WEATHER_H
#define WEATHER_H

#include <mutex>
#include <string>
#include <vector>

// Current weather and a short forecast from Open-Meteo (free, no API key). Fetched in a
// background task every 30 minutes; the home screen reads the latest snapshot.
class Weather {
public:
    struct Day {
        int weekday = 0;  // 0 = Sunday
        int code = 0;     // WMO weather code
        int t_min = 0;
        int t_max = 0;
    };
    struct Snapshot {
        bool valid = false;
        std::string place;
        float temperature = 0;
        float feels_like = 0;
        int humidity = 0;
        float wind_kmh = 0;
        int code = 0;
        std::vector<Day> days;  // today first
        int64_t fetched_us = 0;
    };

    static Weather& GetInstance();

    // Starts fetching (once Wi-Fi is up) and refreshes every 30 minutes.
    void Start();
    Snapshot Get() const;
    // Refetch soon, e.g. after the place changed.
    void RefreshSoon();
    // Looks up a city name with Open-Meteo geocoding and stores it as the weather place.
    bool SetPlace(const std::string& city, std::string& message);
    void RegisterTools();

    // Short Vietnamese description of a WMO weather code ("Mưa rào").
    static const char* Describe(int code);

private:
    Weather();
    static void TaskMain(void* arg);
    bool Fetch();

    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::string place_;
    double latitude_ = 0;
    double longitude_ = 0;
    volatile bool refresh_ = false;
    bool started_ = false;
};

#endif  // WEATHER_H
