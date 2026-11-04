#include "weather.h"

#include "board.h"
#include "mcp_server.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#define TAG "Weather"

namespace {
// Default place: Thanh Hóa city.
constexpr double kDefaultLatitude = 19.8067;
constexpr double kDefaultLongitude = 105.7852;
constexpr const char* kDefaultPlace = "Thanh Hóa";
constexpr int64_t kRefreshUs = 30LL * 60 * 1000 * 1000;
constexpr int64_t kRetryUs = 2LL * 60 * 1000 * 1000;

std::string UrlEncode(const std::string& text) {
    std::string out;
    char buffer[4];
    for (unsigned char c : text) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            snprintf(buffer, sizeof(buffer), "%%%02X", c);
            out += buffer;
        }
    }
    return out;
}

// GET `url` and return the body, or "" on failure.
std::string HttpGet(const std::string& url) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    http->SetTimeout(10000);
    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGW(TAG, "Cannot open %s", url.c_str());
        return "";
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        ESP_LOGW(TAG, "HTTP status %d for %s", status ? *status : -1, url.c_str());
        http->Close();
        return "";
    }
    std::string body = http->ReadAll();
    http->Close();
    return body;
}

double NumberAt(cJSON* array, int index) {
    cJSON* item = cJSON_GetArrayItem(array, index);
    return cJSON_IsNumber(item) ? item->valuedouble : 0;
}

int Round(double value) { return static_cast<int>(value >= 0 ? value + 0.5 : value - 0.5); }
}  // namespace

Weather& Weather::GetInstance() {
    static Weather instance;
    return instance;
}

Weather::Weather() {
    Settings settings("weather");
    place_ = settings.GetString("place", kDefaultPlace);
    // NVS has no double type, so the coordinates are kept in micro-degrees.
    latitude_ = settings.GetInt("lat_u", static_cast<int32_t>(kDefaultLatitude * 1e6)) / 1e6;
    longitude_ = settings.GetInt("lon_u", static_cast<int32_t>(kDefaultLongitude * 1e6)) / 1e6;
}

const char* Weather::Describe(int code) {
    if (code == 0) return "Trời quang";
    if (code == 1) return "Ít mây";
    if (code == 2) return "Có mây";
    if (code == 3) return "Nhiều mây";
    if (code == 45 || code == 48) return "Sương mù";
    if (code >= 51 && code <= 57) return "Mưa phùn";
    if (code >= 61 && code <= 67) return code >= 65 ? "Mưa to" : "Mưa";
    if (code >= 71 && code <= 77) return "Tuyết";
    if (code >= 80 && code <= 82) return "Mưa rào";
    if (code >= 85 && code <= 86) return "Mưa tuyết";
    if (code >= 95) return "Dông";
    return "Không rõ";
}

Weather::Snapshot Weather::Get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void Weather::RefreshSoon() { refresh_ = true; }

void Weather::Start() {
    if (started_) {
        return;
    }
    started_ = true;
    xTaskCreate(TaskMain, "weather", 6144, this, 2, nullptr);
}

void Weather::TaskMain(void* arg) {
    auto* self = static_cast<Weather*>(arg);
    int64_t next_us = 0;
    while (true) {
        const int64_t now = esp_timer_get_time();
        if (self->refresh_ || now >= next_us) {
            if (!WifiManager::GetInstance().IsConnected()) {
                next_us = now + 10LL * 1000 * 1000;
            } else {
                self->refresh_ = false;
                next_us = now + (self->Fetch() ? kRefreshUs : kRetryUs);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

bool Weather::Fetch() {
    double latitude, longitude;
    std::string place;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latitude = latitude_;
        longitude = longitude_;
        place = place_;
    }
    char url[320];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=4",
             latitude, longitude);
    const std::string body = HttpGet(url);
    if (body.empty()) {
        return false;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Bad JSON");
        return false;
    }
    Snapshot snapshot;
    snapshot.place = place;
    cJSON* current = cJSON_GetObjectItem(root, "current");
    cJSON* daily = cJSON_GetObjectItem(root, "daily");
    if (cJSON_IsObject(current)) {
        auto number = [current](const char* key) {
            cJSON* item = cJSON_GetObjectItem(current, key);
            return cJSON_IsNumber(item) ? item->valuedouble : 0.0;
        };
        snapshot.temperature = number("temperature_2m");
        snapshot.feels_like = number("apparent_temperature");
        snapshot.humidity = Round(number("relative_humidity_2m"));
        snapshot.wind_kmh = number("wind_speed_10m");
        snapshot.code = static_cast<int>(number("weather_code"));
        snapshot.valid = true;
    }
    if (cJSON_IsObject(daily)) {
        cJSON* dates = cJSON_GetObjectItem(daily, "time");
        cJSON* codes = cJSON_GetObjectItem(daily, "weather_code");
        cJSON* highs = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON* lows = cJSON_GetObjectItem(daily, "temperature_2m_min");
        const int count = cJSON_GetArraySize(dates);
        for (int i = 0; i < count && i < 4; ++i) {
            Day day;
            cJSON* date = cJSON_GetArrayItem(dates, i);
            int y = 0, m = 0, d = 0;
            if (cJSON_IsString(date) && sscanf(date->valuestring, "%d-%d-%d", &y, &m, &d) == 3) {
                struct tm tm_day = {};
                tm_day.tm_year = y - 1900;
                tm_day.tm_mon = m - 1;
                tm_day.tm_mday = d;
                tm_day.tm_hour = 12;
                mktime(&tm_day);
                day.weekday = tm_day.tm_wday;
            }
            day.code = static_cast<int>(NumberAt(codes, i));
            day.t_max = Round(NumberAt(highs, i));
            day.t_min = Round(NumberAt(lows, i));
            snapshot.days.push_back(day);
        }
    }
    cJSON_Delete(root);
    snapshot.fetched_us = esp_timer_get_time();
    ESP_LOGI(TAG, "%s: %.1f C, code %d, humidity %d%%", place.c_str(), snapshot.temperature, snapshot.code,
             snapshot.humidity);
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snapshot);
    return snapshot_.valid;
}

bool Weather::SetPlace(const std::string& city, std::string& message) {
    const std::string body = HttpGet("http://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&name=" +
                                     UrlEncode(city));
    cJSON* root = body.empty() ? nullptr : cJSON_Parse(body.c_str());
    cJSON* results = root ? cJSON_GetObjectItem(root, "results") : nullptr;
    cJSON* first = cJSON_GetArrayItem(results, 0);
    cJSON* lat = first ? cJSON_GetObjectItem(first, "latitude") : nullptr;
    cJSON* lon = first ? cJSON_GetObjectItem(first, "longitude") : nullptr;
    cJSON* name = first ? cJSON_GetObjectItem(first, "name") : nullptr;
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
        cJSON_Delete(root);
        message = body.empty() ? "Could not reach the weather service." : "Place not found: " + city;
        return false;
    }
    const std::string place = cJSON_IsString(name) ? name->valuestring : city;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latitude_ = lat->valuedouble;
        longitude_ = lon->valuedouble;
        place_ = place;
    }
    Settings settings("weather", true);
    settings.SetString("place", place);
    settings.SetInt("lat_u", static_cast<int32_t>(lat->valuedouble * 1e6));
    settings.SetInt("lon_u", static_cast<int32_t>(lon->valuedouble * 1e6));
    cJSON_Delete(root);
    RefreshSoon();
    message = "Weather place set to " + place + ".";
    return true;
}

void Weather::RegisterTools() {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool("self.weather.get",
                "Weather for the robot's place (the one shown on its home screen): current weather and "
                "the next days' forecast. Use when the user asks about the weather here. To change the "
                "place (e.g. 'Hà Nội', 'Thanh Hóa'), pass it as `set_place`; leave it empty otherwise.",
                PropertyList({Property("set_place", kPropertyTypeString, std::string(""))}),
                [this](const PropertyList& properties) -> ReturnValue {
                    const std::string place = properties["set_place"].value<std::string>();
                    if (!place.empty()) {
                        std::string message;
                        SetPlace(place, message);
                        return message;
                    }
                    const Snapshot s = Get();
                    if (!s.valid) {
                        RefreshSoon();
                        return std::string("Weather not loaded yet; try again in a minute.");
                    }
                    static const char* const kDays[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
                    char line[160];
                    snprintf(line, sizeof(line), "%s: %.0f°C (cảm giác %.0f°C), %s, độ ẩm %d%%, gió %.0f km/h.",
                             s.place.c_str(), s.temperature, s.feels_like, Describe(s.code), s.humidity,
                             s.wind_kmh);
                    std::string text = line;
                    for (const auto& day : s.days) {
                        snprintf(line, sizeof(line), " %s: %s %d-%d°C.", kDays[day.weekday % 7], Describe(day.code),
                                 day.t_min, day.t_max);
                        text += line;
                    }
                    return text;
                });
}
