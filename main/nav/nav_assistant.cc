#include "nav_assistant.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "gps_reader.h"
#include "mcp_server.h"
#include "nav_cameras.h"
#include "nav_voice.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <wifi_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace nav {
namespace {

constexpr char kTag[] = "Nav";
constexpr int kPort = 8081;
constexpr size_t kMaxBody = 8192;
constexpr size_t kMaxPlaces = 12;
constexpr size_t kMaxUserCameras = 150;  // fits in one NVS string
constexpr double kPi = 3.14159265358979323846;

constexpr int kCameraLookMeters = 700;     // shown on screen from this distance
constexpr int kCameraAlertMeters = 500;    // spoken from this distance
constexpr int kMinAlertSpeedKmh = 15;      // no camera alerts while parked or walking
constexpr int kOverspeedMarginKmh = 3;
constexpr int64_t kSecondUs = 1000LL * 1000;
constexpr int64_t kCameraRepeatUs = 5 * 60 * kSecondUs;
constexpr int64_t kOverspeedRepeatUs = 30 * kSecondUs;
constexpr int64_t kJamCheckUs = 120 * kSecondUs;
constexpr int64_t kJamRepeatUs = 10 * 60 * kSecondUs;

struct Place {
    std::string name;
    double lat = 0;
    double lon = 0;
};

struct UserCamera {
    double lat = 0;
    double lon = 0;
    int limit_kmh = 0;
};

struct Config {
    std::string key;              // TomTom API key
    std::string mode = "car";     // car | motorcycle
    int limit_kmh = 0;            // the user's own speed limit, 0 = off
    std::vector<Place> places;
    std::vector<UserCamera> cameras;
    int am_minute = -1;           // morning check, minutes after midnight, -1 = off
    std::string am_from;
    std::string am_to;
    int am_delay_min = 10;        // "leave early" when the delay is at least this
    bool am_weekdays = true;      // Monday to Friday only
};

struct Route {
    bool ok = false;
    std::string error;
    int length_m = 0;
    int time_s = 0;
    int delay_s = 0;
};

struct Flow {
    bool ok = false;
    std::string error;
    int current_kmh = 0;
    int free_kmh = 0;
    bool closed = false;
    float confidence = 0;
};

std::mutex mutex_;
Config config_;
std::function<void(const Status&)> status_listener_;
std::function<void()> setup_viewer_;
std::function<void(int)> drive_viewer_;
std::string note_;            // last traffic alert for the driving screen
int64_t note_us_ = 0;
std::function<bool()> can_speak_;
httpd_handle_t server_ = nullptr;
TaskHandle_t task_ = nullptr;
Status last_status_;

// ---------- small helpers ----------

double Radians(double degrees) { return degrees * kPi / 180.0; }

double DistanceMeters(double lat1, double lon1, double lat2, double lon2) {
    const double dlat = Radians(lat2 - lat1);
    const double dlon = Radians(lon2 - lon1);
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(Radians(lat1)) * std::cos(Radians(lat2)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 6371000.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double BearingDegrees(double lat1, double lon1, double lat2, double lon2) {
    const double y = std::sin(Radians(lon2 - lon1)) * std::cos(Radians(lat2));
    const double x = std::cos(Radians(lat1)) * std::sin(Radians(lat2)) -
                     std::sin(Radians(lat1)) * std::cos(Radians(lat2)) * std::cos(Radians(lon2 - lon1));
    const double bearing = std::atan2(y, x) * 180.0 / kPi;
    return bearing < 0 ? bearing + 360 : bearing;
}

double AngleDiff(double a, double b) {
    double diff = std::fabs(a - b);
    return diff > 180 ? 360 - diff : diff;
}

// The point `meters` away along `course_deg`.
void PointAhead(double lat, double lon, double course_deg, double meters, double& out_lat, double& out_lon) {
    const double d = meters / 6371000.0;
    const double brng = Radians(course_deg);
    const double lat1 = Radians(lat);
    const double lon1 = Radians(lon);
    const double lat2 = std::asin(std::sin(lat1) * std::cos(d) + std::cos(lat1) * std::sin(d) * std::cos(brng));
    const double lon2 = lon1 + std::atan2(std::sin(brng) * std::sin(d) * std::cos(lat1),
                                          std::cos(d) - std::sin(lat1) * std::sin(lat2));
    out_lat = lat2 * 180.0 / kPi;
    out_lon = lon2 * 180.0 / kPi;
}

std::string Coord(double value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = Trim(text.substr(start, end - start));
        if (!line.empty()) {
            lines.push_back(line);
        }
        start = end + 1;
    }
    return lines;
}

// "21.0285, 105.8542" (Google Maps copies it like this) -> true.
bool ParseLatLon(const std::string& text, double& lat, double& lon) {
    const char* p = text.c_str();
    char* end = nullptr;
    lat = strtod(p, &end);
    if (end == p) {
        return false;
    }
    while (*end == ' ' || *end == ',' || *end == ';') {
        ++end;
    }
    const char* q = end;
    lon = strtod(q, &end);
    if (end == q) {
        return false;
    }
    while (*end == ' ') {
        ++end;
    }
    return *end == '\0' && std::fabs(lat) <= 90 && std::fabs(lon) <= 180 && (lat != 0 || lon != 0);
}

std::string UrlEncode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

std::string UrlDecode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        } else if (text[i] == '%' && i + 2 < text.size()) {
            out += static_cast<char>(strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::map<std::string, std::string> ParseForm(const std::string& body) {
    std::map<std::string, std::string> form;
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find('&', start);
        if (end == std::string::npos) {
            end = body.size();
        }
        const std::string pair = body.substr(start, end - start);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            form[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
        }
        start = end + 1;
    }
    return form;
}

std::string HtmlEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Lowercase ASCII without spaces, so "Nha" / "nhà " / "Nhà" still compare well enough after the
// assistant spells the name back.
std::string NameKey(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c != ' ') {
            out += static_cast<char>(c < 128 ? tolower(c) : c);
        }
    }
    return out;
}

std::string Minutes(int seconds) {
    const int minutes = (seconds + 30) / 60;
    if (minutes < 60) {
        return std::to_string(minutes) + " phút";
    }
    return std::to_string(minutes / 60) + " giờ " + std::to_string(minutes % 60) + " phút";
}

std::string Km(int meters) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f km", meters / 1000.0);
    return buffer;
}

void Notify(const std::string& text, int ms = 6000) {
    if (auto* display = Board::GetInstance().GetDisplay()) {
        display->ShowNotification(text, ms);
    }
}

void Speak(nav_voice::Id id) {
    if (can_speak_ && !can_speak_()) {
        return;
    }
    auto& app = Application::GetInstance();
    // While the assistant listens or talks the alert would mix with the conversation; the
    // notification on screen is enough then.
    if (app.GetDeviceState() != kDeviceStateIdle) {
        return;
    }
    app.PlaySound(nav_voice::Clip(id));
}

// ---------- settings ----------

void LoadConfig() {
    Settings settings("nav", false);
    Config config;
    config.key = settings.GetString("key");
    config.mode = settings.GetString("mode", "car");
    config.limit_kmh = settings.GetInt("limit", 0);
    config.am_minute = settings.GetInt("am_min", -1);
    config.am_from = settings.GetString("am_from");
    config.am_to = settings.GetString("am_to");
    config.am_delay_min = settings.GetInt("am_delay", 10);
    config.am_weekdays = settings.GetBool("am_wd", true);
    for (const auto& line : SplitLines(settings.GetString("places"))) {
        const size_t a = line.find('|');
        const size_t b = a == std::string::npos ? a : line.find('|', a + 1);
        if (b == std::string::npos) {
            continue;
        }
        Place place;
        place.name = line.substr(0, a);
        place.lat = strtod(line.substr(a + 1, b - a - 1).c_str(), nullptr);
        place.lon = strtod(line.substr(b + 1).c_str(), nullptr);
        config.places.push_back(place);
    }
    for (const auto& line : SplitLines(settings.GetString("cams"))) {
        UserCamera camera;
        if (sscanf(line.c_str(), "%lf,%lf,%d", &camera.lat, &camera.lon, &camera.limit_kmh) >= 2) {
            config.cameras.push_back(camera);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

// Call with mutex_ held.
void SaveConfigLocked() {
    Settings settings("nav", true);
    settings.SetString("key", config_.key);
    settings.SetString("mode", config_.mode);
    settings.SetInt("limit", config_.limit_kmh);
    settings.SetInt("am_min", config_.am_minute);
    settings.SetString("am_from", config_.am_from);
    settings.SetString("am_to", config_.am_to);
    settings.SetInt("am_delay", config_.am_delay_min);
    settings.SetBool("am_wd", config_.am_weekdays);
    std::string places;
    for (const auto& place : config_.places) {
        places += place.name + "|" + Coord(place.lat) + "|" + Coord(place.lon) + "\n";
    }
    settings.SetString("places", places);
    std::string cameras;
    for (const auto& camera : config_.cameras) {
        cameras += Coord(camera.lat) + "," + Coord(camera.lon) + "," + std::to_string(camera.limit_kmh) + "\n";
    }
    settings.SetString("cams", cameras);
}

Config GetConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

const Place* FindPlace(const std::vector<Place>& places, const std::string& name) {
    const std::string key = NameKey(name);
    if (key.empty()) {
        return nullptr;
    }
    for (const auto& place : places) {
        if (NameKey(place.name) == key) {
            return &place;
        }
    }
    for (const auto& place : places) {
        const std::string other = NameKey(place.name);
        if (other.find(key) != std::string::npos || key.find(other) != std::string::npos) {
            return &place;
        }
    }
    return nullptr;
}

std::string PlaceNames(const std::vector<Place>& places) {
    std::string names;
    for (const auto& place : places) {
        names += (names.empty() ? "" : ", ") + place.name;
    }
    return names.empty() ? std::string("(chưa lưu địa điểm nào)") : names;
}

// ---------- TomTom ----------

bool HttpGetJson(const std::string& url, cJSON*& json, std::string& error) {
    json = nullptr;
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(4);
    http->SetTimeout(10000);
    if (auto opened = http->Open("GET", url); !opened) {
        error = "không kết nối được TomTom (kiểm tra mạng)";
        return false;
    }
    auto status = http->GetStatusCode();
    const std::string body = http->ReadAll();
    http->Close();
    if (!status || *status != 200) {
        const int code = status ? *status : 0;
        error = code == 403 || code == 401 ? "key TomTom sai hoặc hết lượt"
                                           : "TomTom trả lỗi HTTP " + std::to_string(code);
        ESP_LOGW(kTag, "TomTom HTTP %d", code);
        return false;
    }
    json = cJSON_Parse(body.c_str());
    if (json == nullptr) {
        error = "TomTom trả dữ liệu lạ";
        return false;
    }
    return true;
}

bool Geocode(const std::string& key, const std::string& query, double& lat, double& lon, std::string& error) {
    if (key.empty()) {
        error = "chưa có key TomTom, nhập tọa độ (vd 19.8075, 105.7764) thay cho địa chỉ";
        return false;
    }
    const std::string url = "https://api.tomtom.com/search/2/geocode/" + UrlEncode(query) +
                            ".json?limit=1&countrySet=VN&language=vi-VN&key=" + UrlEncode(key);
    cJSON* json = nullptr;
    if (!HttpGetJson(url, json, error)) {
        return false;
    }
    bool ok = false;
    cJSON* results = cJSON_GetObjectItem(json, "results");
    cJSON* first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
    cJSON* position = first ? cJSON_GetObjectItem(first, "position") : nullptr;
    cJSON* jlat = position ? cJSON_GetObjectItem(position, "lat") : nullptr;
    cJSON* jlon = position ? cJSON_GetObjectItem(position, "lon") : nullptr;
    if (cJSON_IsNumber(jlat) && cJSON_IsNumber(jlon)) {
        lat = jlat->valuedouble;
        lon = jlon->valuedouble;
        ok = true;
    } else {
        error = "không tìm thấy địa chỉ \"" + query + "\"";
    }
    cJSON_Delete(json);
    return ok;
}

Route GetRoute(const Config& config, double lat1, double lon1, double lat2, double lon2) {
    Route route;
    if (config.key.empty()) {
        route.error = "chưa có key TomTom";
        return route;
    }
    const std::string url = "https://api.tomtom.com/routing/1/calculateRoute/" + Coord(lat1) + "," + Coord(lon1) +
                            ":" + Coord(lat2) + "," + Coord(lon2) +
                            "/json?traffic=true&routeType=fastest&routeRepresentation=summaryOnly&travelMode=" +
                            (config.mode == "motorcycle" ? std::string("motorcycle") : std::string("car")) +
                            "&key=" + UrlEncode(config.key);
    cJSON* json = nullptr;
    if (!HttpGetJson(url, json, route.error)) {
        return route;
    }
    cJSON* routes = cJSON_GetObjectItem(json, "routes");
    cJSON* first = cJSON_IsArray(routes) ? cJSON_GetArrayItem(routes, 0) : nullptr;
    cJSON* summary = first ? cJSON_GetObjectItem(first, "summary") : nullptr;
    auto number = [summary](const char* name) {
        cJSON* item = summary ? cJSON_GetObjectItem(summary, name) : nullptr;
        return cJSON_IsNumber(item) ? item->valueint : 0;
    };
    if (summary != nullptr) {
        route.ok = true;
        route.length_m = number("lengthInMeters");
        route.time_s = number("travelTimeInSeconds");
        route.delay_s = number("trafficDelayInSeconds");
    } else {
        route.error = "TomTom không tìm được đường";
    }
    cJSON_Delete(json);
    return route;
}

Flow GetFlow(const std::string& key, double lat, double lon) {
    Flow flow;
    if (key.empty()) {
        flow.error = "chưa có key TomTom";
        return flow;
    }
    const std::string url = "https://api.tomtom.com/traffic/services/4/flowSegmentData/absolute/10/json?unit=KMPH&point=" +
                            Coord(lat) + "," + Coord(lon) + "&key=" + UrlEncode(key);
    cJSON* json = nullptr;
    if (!HttpGetJson(url, json, flow.error)) {
        return flow;
    }
    cJSON* data = cJSON_GetObjectItem(json, "flowSegmentData");
    cJSON* current = data ? cJSON_GetObjectItem(data, "currentSpeed") : nullptr;
    cJSON* free_flow = data ? cJSON_GetObjectItem(data, "freeFlowSpeed") : nullptr;
    if (cJSON_IsNumber(current) && cJSON_IsNumber(free_flow)) {
        flow.ok = true;
        flow.current_kmh = current->valueint;
        flow.free_kmh = free_flow->valueint;
        cJSON* closure = cJSON_GetObjectItem(data, "roadClosure");
        flow.closed = cJSON_IsTrue(closure);
        cJSON* confidence = cJSON_GetObjectItem(data, "confidence");
        flow.confidence = cJSON_IsNumber(confidence) ? static_cast<float>(confidence->valuedouble) : 0;
    } else {
        flow.error = "TomTom không có dữ liệu giao thông ở đây";
    }
    cJSON_Delete(json);
    return flow;
}

std::string RouteSentence(const std::string& from, const std::string& to, const Route& route) {
    std::string text = "Từ " + from + " đến " + to + ": " + Km(route.length_m) + ", khoảng " + Minutes(route.time_s) +
                       " với giao thông lúc này";
    if (route.delay_s >= 60) {
        text += " (chậm hơn bình thường " + Minutes(route.delay_s) + " do đông xe)";
    } else {
        text += " (đường thông thoáng)";
    }
    return text + ".";
}

// ---------- monitor ----------

struct Monitor {
    std::map<int, int64_t> camera_alerts;  // camera id -> last alert
    int over_count = 0;
    int64_t last_over_us = -kOverspeedRepeatUs;
    int64_t last_jam_check_us = 0;
    int64_t last_jam_alert_us = -kJamRepeatUs;
    bool gps_announced = false;
    bool module_was_seen = false;
    int64_t module_lost_us = 0;
    int moving_seconds = 0;
    int stopped_seconds = 0;
    bool drive_opened = false;    // opened by itself on this trip
    int am_done_day = -1;
};

void MorningCheck(const Config& config) {
    const Place* from = FindPlace(config.places, config.am_from);
    const Place* to = FindPlace(config.places, config.am_to);
    if (from == nullptr || to == nullptr) {
        Notify("Báo đường buổi sáng: chưa có địa điểm " + (from ? config.am_to : config.am_from), 10000);
        return;
    }
    const Route route = GetRoute(config, from->lat, from->lon, to->lat, to->lon);
    if (!route.ok) {
        Notify("Báo đường buổi sáng: " + route.error, 10000);
        return;
    }
    const bool slow = route.delay_s >= config.am_delay_min * 60;
    std::string text = "Đi " + to->name + ": " + Minutes(route.time_s);
    if (route.delay_s >= 60) {
        text += ", chậm " + Minutes(route.delay_s);
    }
    ESP_LOGI(kTag, "Morning check: %d s, delay %d s", route.time_s, route.delay_s);
    Notify(text, 30000);
    Speak(slow ? nav_voice::Id::k_leave_early : nav_voice::Id::k_road_ok);
}

void CheckMorning(Monitor& monitor, const Config& config) {
    if (config.am_minute < 0) {
        return;
    }
    const time_t now = time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    if (local.tm_year + 1900 < 2025) {
        return;  // clock not set yet
    }
    if (config.am_weekdays && (local.tm_wday == 0 || local.tm_wday == 6)) {
        return;
    }
    const int minute = local.tm_hour * 60 + local.tm_min;
    if (minute != config.am_minute || monitor.am_done_day == local.tm_yday) {
        return;
    }
    monitor.am_done_day = local.tm_yday;
    MorningCheck(config);
}

void Tick(Monitor& monitor) {
    const Config config = GetConfig();
    auto& gps = GpsReader::GetInstance();
    const GpsReader::Fix fix = gps.GetFix();
    const bool module_fix = gps.HasFreshFix();
    const int64_t now = esp_timer_get_time();

    Status status;
    status.module_seen = gps.ModuleSeen();
    status.has_fix = fix.valid;
    status.satellites = fix.satellites;
    status.speed_kmh = module_fix ? static_cast<int>(fix.speed_kmh + 0.5f) : 0;
    status.limit_kmh = config.limit_kmh;

    status.lat = fix.lat;
    status.lon = fix.lon;
    status.course_deg = fix.course_deg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!note_.empty() && now - note_us_ < 10 * 60 * kSecondUs) {
            status.note = note_;
        }
    }

    // Tell the user when the module is plugged in or unplugged, and when it finds satellites.
    if (status.module_seen && !monitor.module_was_seen) {
        monitor.module_was_seen = true;
        monitor.module_lost_us = 0;
        ESP_LOGI(kTag, "GPS module connected");
        Notify("Đã kết nối GPS, đang tìm vệ tinh", 8000);
        if (!monitor.gps_announced) {
            Speak(nav_voice::Id::k_gps_module);
        }
    } else if (!status.module_seen && monitor.module_was_seen) {
        if (monitor.module_lost_us == 0) {
            monitor.module_lost_us = now;
        } else if (now - monitor.module_lost_us > 5 * kSecondUs) {
            monitor.module_was_seen = false;
            ESP_LOGW(kTag, "GPS module lost");
            Notify("Mất kết nối GPS: kiểm tra dây", 8000);
        }
    } else {
        monitor.module_lost_us = 0;
    }
    if (module_fix && !monitor.gps_announced) {
        monitor.gps_announced = true;
        Notify("Đã bắt được tín hiệu GPS (" + std::to_string(fix.satellites) + " vệ tinh)", 8000);
        Speak(nav_voice::Id::k_gps_ok);
    }

    // The driving screen opens by itself once the car has been moving for 5 s, once per trip
    // (a trip ends after 5 minutes stopped).
    if (module_fix && status.speed_kmh >= kMinAlertSpeedKmh) {
        monitor.stopped_seconds = 0;
        if (++monitor.moving_seconds >= 5 && !monitor.drive_opened) {
            monitor.drive_opened = true;
            if (drive_viewer_) {
                drive_viewer_(1);
            }
        }
    } else {
        monitor.moving_seconds = 0;
        if (status.speed_kmh < 5 && ++monitor.stopped_seconds >= 300) {
            monitor.drive_opened = false;
        }
    }

    if (module_fix) {
        const bool moving = status.speed_kmh >= kMinAlertSpeedKmh;
        // The nearest camera ahead (any direction while slow, the course is noise then).
        int best_id = -1;
        double best_m = kCameraLookMeters + 1;
        int best_limit = 0;
        auto consider = [&](int id, double lat, double lon, int limit) {
            const double meters = DistanceMeters(fix.lat, fix.lon, lat, lon);
            if (meters > kCameraLookMeters || meters >= best_m) {
                return;
            }
            if (moving && meters > 40 &&
                AngleDiff(fix.course_deg, BearingDegrees(fix.lat, fix.lon, lat, lon)) > 60) {
                return;  // behind or beside the car
            }
            best_id = id;
            best_m = meters;
            best_limit = limit;
        };
        for (size_t i = 0; i < nav_cameras::kBuiltInCount; ++i) {
            const auto& camera = nav_cameras::kBuiltIn[i];
            consider(static_cast<int>(i), camera.lat, camera.lon, camera.limit_kmh);
        }
        for (size_t i = 0; i < config.cameras.size(); ++i) {
            const auto& camera = config.cameras[i];
            consider(10000 + static_cast<int>(i), camera.lat, camera.lon, camera.limit_kmh);
        }
        if (best_id >= 0) {
            status.camera_m = static_cast<int>(best_m);
            if (best_limit > 0) {
                status.limit_kmh = status.limit_kmh > 0 ? std::min(status.limit_kmh, best_limit) : best_limit;
            }
            auto it = monitor.camera_alerts.find(best_id);
            if (moving && best_m <= kCameraAlertMeters &&
                (it == monitor.camera_alerts.end() || now - it->second > kCameraRepeatUs)) {
                monitor.camera_alerts[best_id] = now;
                std::string text = "Camera phía trước " + std::to_string(static_cast<int>(best_m)) + " m";
                if (best_limit > 0) {
                    text += ", tối đa " + std::to_string(best_limit) + " km/h";
                }
                Notify(text, 8000);
                Speak(nav_voice::Id::k_camera);
            }
        }

        if (status.limit_kmh > 0 && status.speed_kmh > status.limit_kmh + kOverspeedMarginKmh) {
            status.overspeed = true;
            if (++monitor.over_count >= 3 && now - monitor.last_over_us > kOverspeedRepeatUs) {
                monitor.last_over_us = now;
                Notify("Quá tốc độ: " + std::to_string(status.speed_kmh) + "/" +
                           std::to_string(status.limit_kmh) + " km/h",
                       5000);
                Speak(nav_voice::Id::k_overspeed);
            }
        } else {
            monitor.over_count = 0;
        }

        // Traffic about 1 km ahead, every 2 minutes while driving.
        if (!config.key.empty() && moving && now - monitor.last_jam_check_us > kJamCheckUs) {
            monitor.last_jam_check_us = now;
            double lat = 0, lon = 0;
            PointAhead(fix.lat, fix.lon, fix.course_deg, 1000, lat, lon);
            const Flow flow = GetFlow(config.key, lat, lon);
            const bool jam = flow.ok && flow.confidence >= 0.5f &&
                             (flow.closed || (flow.free_kmh >= 25 && flow.current_kmh * 100 < flow.free_kmh * 45));
            if (jam && now - monitor.last_jam_alert_us > kJamRepeatUs) {
                monitor.last_jam_alert_us = now;
                const std::string text = flow.closed ? std::string("Phía trước: đường bị đóng")
                                                     : "Tắc phía trước: " + std::to_string(flow.current_kmh) +
                                                           " km/h (bình thường " + std::to_string(flow.free_kmh) + ")";
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    note_ = text;
                    note_us_ = now;
                }
                Notify(text, 10000);
                Speak(nav_voice::Id::k_jam);
            }
        }
    }

    CheckMorning(monitor, config);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_status_ = status;
    }
    if (status_listener_) {
        status_listener_(status);
    }
}

void MonitorTask(void*) {
    Monitor monitor;
    while (true) {
        Tick(monitor);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---------- web page ----------

std::string ConfigPage(const std::string& message) {
    const Config config = GetConfig();
    Status status;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status = last_status_;
    }
    std::string gps;
    if (!status.module_seen) {
        gps = status.has_fix ? "Không thấy module GPS, đang dùng vị trí điện thoại gửi." : "Chưa thấy module GPS (kiểm tra dây TX vào GPIO 5).";
    } else if (!status.has_fix) {
        gps = "Module GPS có điện, đang dò vệ tinh (" + std::to_string(status.satellites) + " vệ tinh).";
    } else {
        gps = "GPS tốt: " + std::to_string(status.satellites) + " vệ tinh, " + std::to_string(status.speed_kmh) + " km/h.";
    }
    std::string places;
    for (const auto& place : config.places) {
        places += place.name + " | " + Coord(place.lat) + ", " + Coord(place.lon) + "\n";
    }
    std::string cameras;
    for (const auto& camera : config.cameras) {
        cameras += Coord(camera.lat) + ", " + Coord(camera.lon);
        if (camera.limit_kmh > 0) {
            cameras += ", " + std::to_string(camera.limit_kmh);
        }
        cameras += "\n";
    }
    char am_time[24] = "";
    if (config.am_minute >= 0) {
        snprintf(am_time, sizeof(am_time), "%02d:%02d", (config.am_minute / 60) % 24, config.am_minute % 60);
    }
    std::string page =
        "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Robot trên xe</title><style>body{font-family:sans-serif;margin:16px;max-width:560px}"
        "label{display:block;margin-top:12px;font-weight:bold}input,textarea,select{width:100%;box-sizing:border-box;"
        "font-size:16px;padding:6px}textarea{height:110px}small{color:#666}.msg{background:#e7f6e7;padding:8px}"
        "button{margin-top:16px;font-size:18px;padding:10px;width:100%}</style></head><body>"
        "<h2>Robot trên xe</h2>";
    if (!message.empty()) {
        page += "<p class=msg>" + HtmlEscape(message) + "</p>";
    }
    page += "<p>" + HtmlEscape(gps) + "</p><form method=post action=/xe>";
    page += "<label>Key TomTom</label><input name=key autocomplete=off placeholder='" +
            std::string(config.key.empty() ? "chưa có, dán key vào đây" : "đã có key (để trống để giữ nguyên)") +
            "'><small>Đăng ký miễn phí tại developer.tomtom.com, tạo key rồi dán vào. Gõ - để xóa key.</small>";
    page += "<label>Đi bằng</label><select name=mode><option value=car" +
            std::string(config.mode != "motorcycle" ? " selected" : "") + ">Ô tô</option><option value=motorcycle" +
            std::string(config.mode == "motorcycle" ? " selected" : "") + ">Xe máy</option></select>";
    page += "<label>Tốc độ tối đa của bạn (km/h, 0 = tắt)</label><input name=limit type=number min=0 max=150 value=" +
            std::to_string(config.limit_kmh) + "><small>Gần camera có ghi tốc độ thì robot lấy số nhỏ hơn.</small>";
    page += "<label>Địa điểm (mỗi dòng: Tên | địa chỉ hoặc tọa độ)</label><textarea name=places>" +
            HtmlEscape(places) +
            "</textarea><small>Ví dụ: Trường | 19.8075, 105.7764. Tọa độ: trên Google Maps nhấn giữ một điểm "
            "rồi chép số. Ghi địa chỉ thì cần key TomTom.</small>";
    page += "<label>Camera bạn tự thêm (mỗi dòng: vĩ độ, kinh độ, tốc độ tối đa nếu biết)</label><textarea name=cams>" +
            HtmlEscape(cameras) + "</textarea><small>Robot có sẵn " + std::to_string(nav_cameras::kBuiltInCount) +
            " camera từ OpenStreetMap. Đang lái thì nói \"đánh dấu camera ở đây\".</small>";
    page += "<h3>Báo đường buổi sáng</h3><label>Giờ kiểm tra (để trống = tắt)</label><input name=am_time type=time value='" +
            std::string(am_time) + "'>";
    page += "<label>Từ</label><input name=am_from value='" + HtmlEscape(config.am_from) +
            "' placeholder=Nhà><label>Đến</label><input name=am_to value='" + HtmlEscape(config.am_to) +
            "' placeholder=Trường>";
    page += "<label>Nhắc đi sớm khi chậm hơn (phút)</label><input name=am_delay type=number min=1 max=120 value=" +
            std::to_string(config.am_delay_min) + ">";
    page += "<label><input type=checkbox name=am_wd value=1 style='width:auto'" +
            std::string(config.am_weekdays ? " checked" : "") + "> Chỉ thứ Hai đến thứ Sáu</label>";
    page += "<button type=submit>Lưu</button></form>";
    page += "<h3>Gửi vị trí từ iPhone (khi chưa có module GPS)</h3><p><small>Phím tắt: thêm "
            "\"Lấy vị trí hiện tại\", rồi \"Lấy nội dung của URL\" với địa chỉ<br>http://&lt;IP robot&gt;:8081/xe/vitri?lat=[Vĩ độ]&amp;lon=[Kinh độ]</small></p>";
    page += "</body></html>";
    return page;
}

esp_err_t SendPage(httpd_req_t* req, const std::string& message) {
    const std::string page = ConfigPage(message);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page.data(), page.size());
}

esp_err_t HandleGet(httpd_req_t* req) { return SendPage(req, std::string()); }

esp_err_t HandlePost(httpd_req_t* req) {
    const size_t length = req->content_len;
    if (length == 0 || length > kMaxBody) {
        return SendPage(req, "Dữ liệu quá dài.");
    }
    std::string body(length, '\0');
    size_t total = 0;
    while (total < length) {
        const int read = httpd_req_recv(req, body.data() + total, length - total);
        if (read <= 0) {
            if (read == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        total += read;
    }
    auto form = ParseForm(body);
    Config config = GetConfig();
    std::vector<std::string> problems;

    const std::string key = Trim(form["key"]);
    if (key == "-") {
        config.key.clear();
    } else if (!key.empty()) {
        config.key = key;
    }
    config.mode = form["mode"] == "motorcycle" ? "motorcycle" : "car";
    config.limit_kmh = std::max(0, std::min(150, atoi(form["limit"].c_str())));

    std::vector<Place> places;
    for (const auto& line : SplitLines(form["places"])) {
        if (places.size() >= kMaxPlaces) {
            problems.push_back("chỉ lưu " + std::to_string(kMaxPlaces) + " địa điểm");
            break;
        }
        const size_t bar = line.find('|');
        if (bar == std::string::npos) {
            problems.push_back("dòng \"" + line + "\" thiếu dấu |");
            continue;
        }
        Place place;
        place.name = Trim(line.substr(0, bar));
        const std::string where = Trim(line.substr(bar + 1));
        std::string error;
        if (place.name.empty() || where.empty()) {
            problems.push_back("dòng \"" + line + "\" thiếu tên hoặc địa chỉ");
        } else if (ParseLatLon(where, place.lat, place.lon) || Geocode(config.key, where, place.lat, place.lon, error)) {
            places.push_back(place);
        } else {
            problems.push_back(place.name + ": " + error);
        }
    }
    config.places = places;

    std::vector<UserCamera> cameras;
    for (const auto& line : SplitLines(form["cams"])) {
        UserCamera camera;
        if (sscanf(line.c_str(), "%lf , %lf , %d", &camera.lat, &camera.lon, &camera.limit_kmh) >= 2 &&
            std::fabs(camera.lat) <= 90 && std::fabs(camera.lon) <= 180) {
            if (cameras.size() < kMaxUserCameras) {
                cameras.push_back(camera);
            }
        } else {
            problems.push_back("camera \"" + line + "\" sai dạng");
        }
    }
    config.cameras = cameras;

    const std::string am_time = Trim(form["am_time"]);
    int hour = 0, minute = 0;
    config.am_minute = sscanf(am_time.c_str(), "%d:%d", &hour, &minute) == 2 && hour >= 0 && hour < 24 &&
                               minute >= 0 && minute < 60
                           ? hour * 60 + minute
                           : -1;
    config.am_from = Trim(form["am_from"]);
    config.am_to = Trim(form["am_to"]);
    config.am_delay_min = std::max(1, std::min(120, atoi(form["am_delay"].c_str())));
    config.am_weekdays = form.count("am_wd") > 0;
    if (config.am_minute >= 0 &&
        (FindPlace(config.places, config.am_from) == nullptr || FindPlace(config.places, config.am_to) == nullptr)) {
        problems.push_back("báo đường buổi sáng: \"Từ\" và \"Đến\" phải là tên trong danh sách địa điểm");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        SaveConfigLocked();
    }
    std::string message = "Đã lưu.";
    for (const auto& problem : problems) {
        message += " Lưu ý: " + problem + ".";
    }
    return SendPage(req, message);
}

esp_err_t HandleLocation(httpd_req_t* req) {
    char query[128] = {};
    char lat[24] = {};
    char lon[24] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "lat", lat, sizeof(lat)) != ESP_OK ||
        httpd_query_key_value(query, "lon", lon, sizeof(lon)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Cần lat và lon.");
        return ESP_FAIL;
    }
    // iPhone in Vietnamese may write the decimal comma.
    for (char* p : {lat, lon}) {
        for (char* c = p; *c; ++c) {
            if (*c == ',') {
                *c = '.';
            }
        }
    }
    double la = 0, lo = 0;
    if (!ParseLatLon(std::string(lat) + "," + lon, la, lo)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Tọa độ sai.");
        return ESP_FAIL;
    }
    GpsReader::GetInstance().SetPhoneFix(la, lo);
    ESP_LOGI(kTag, "Phone location received");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "Robot đã nhận vị trí.", HTTPD_RESP_USE_STRLEN);
}

bool StartServer() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_ != nullptr) {
        return true;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kPort;
    config.ctrl_port = 32771;  // 32768 Wi-Fi setup, 32770 photo page
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.stack_size = 8192;  // geocoding runs TLS in the handler
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot start the car page on port %d", kPort);
        server_ = nullptr;
        return false;
    }
    httpd_uri_t get = {};
    get.uri = "/xe";
    get.method = HTTP_GET;
    get.handler = HandleGet;
    httpd_register_uri_handler(server_, &get);
    httpd_uri_t post = get;
    post.method = HTTP_POST;
    post.handler = HandlePost;
    httpd_register_uri_handler(server_, &post);
    httpd_uri_t location = get;
    location.uri = "/xe/vitri";
    location.handler = HandleLocation;
    httpd_register_uri_handler(server_, &location);
    ESP_LOGI(kTag, "Car page on port %d", kPort);
    return true;
}

// Position for a question: GPS (or phone) when known, else the place called "Nhà".
bool CurrentOrigin(const Config& config, double& lat, double& lon, std::string& label) {
    const GpsReader::Fix fix = GpsReader::GetInstance().GetFix();
    if (fix.valid) {
        lat = fix.lat;
        lon = fix.lon;
        label = "vị trí hiện tại";
        return true;
    }
    if (const Place* home = FindPlace(config.places, "Nhà")) {
        lat = home->lat;
        lon = home->lon;
        label = home->name;
        return true;
    }
    return false;
}

}  // namespace

void SetStatusListener(std::function<void(const Status&)> listener) { status_listener_ = std::move(listener); }
void SetDriveViewer(std::function<void(int)> viewer) { drive_viewer_ = std::move(viewer); }

void MapCenter(double& lat, double& lon, std::string& label) {
    const GpsReader::Fix fix = GpsReader::GetInstance().GetFix();
    if (fix.valid) {
        lat = fix.lat;
        lon = fix.lon;
        label = GpsReader::GetInstance().HasFreshFix() ? "GPS" : "vị trí điện thoại";
        return;
    }
    const Config config = GetConfig();
    const Place* place = FindPlace(config.places, "Nhà");
    if (place == nullptr && !config.places.empty()) {
        place = &config.places[0];
    }
    if (place != nullptr) {
        lat = place->lat;
        lon = place->lon;
        label = place->name;
        return;
    }
    lat = 19.8067;  // Thanh Hóa city
    lon = 105.7852;
    label = "Thanh Hóa";
}

std::vector<std::pair<double, double>> CamerasNear(double lat, double lon, double meters) {
    std::vector<std::pair<double, double>> cameras;
    for (size_t i = 0; i < nav_cameras::kBuiltInCount; ++i) {
        const auto& camera = nav_cameras::kBuiltIn[i];
        if (DistanceMeters(lat, lon, camera.lat, camera.lon) <= meters) {
            cameras.emplace_back(camera.lat, camera.lon);
        }
    }
    const Config config = GetConfig();
    for (const auto& camera : config.cameras) {
        if (DistanceMeters(lat, lon, camera.lat, camera.lon) <= meters) {
            cameras.emplace_back(camera.lat, camera.lon);
        }
    }
    return cameras;
}

bool HasKey() { return !GetConfig().key.empty(); }

bool FetchMapTile(int zoom, int x, int y, std::string& png) {
    const std::string key = GetConfig().key;
    if (key.empty()) {
        return false;
    }
    const std::string url = "https://api.tomtom.com/map/1/tile/basic/main/" + std::to_string(zoom) + "/" +
                            std::to_string(x) + "/" + std::to_string(y) + ".png?tileSize=256&key=" + UrlEncode(key);
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
    http->SetTimeout(10000);
    if (auto opened = http->Open("GET", url); !opened) {
        return false;
    }
    auto status = http->GetStatusCode();
    png = http->ReadAll();
    http->Close();
    if (!status || *status != 200) {
        ESP_LOGW(kTag, "Map tile HTTP %d", status ? *status : 0);
        return false;
    }
    return png.size() > 8;
}
void SetSetupViewer(std::function<void()> viewer) { setup_viewer_ = std::move(viewer); }
void SetCanSpeak(std::function<bool()> can_speak) { can_speak_ = std::move(can_speak); }

std::string SetupUrl() {
    const std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty() || !StartServer()) {
        return std::string();
    }
    return "http://" + ip + ":" + std::to_string(kPort) + "/xe";
}

void Start(gpio_num_t rx, gpio_num_t tx) {
    if (task_ != nullptr) {
        return;
    }
    LoadConfig();
    GpsReader::GetInstance().Start(rx, tx);
    StartServer();
    xTaskCreate(MonitorTask, "nav", 8192, nullptr, 2, &task_);
}

void RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.nav.traffic",
        "Live traffic and travel time (TomTom). Use when the user asks how long it takes to go "
        "somewhere, whether the road is jammed (tắc đường, kẹt xe, đi mất bao lâu, đường có đông "
        "không). `destination`: a saved place name (e.g. Trường, Cơ quan) or an address; empty = "
        "traffic on the road where the robot is now. `origin`: saved place or address; empty = "
        "current GPS position, or the place Nhà. Answer in Vietnamese with the result.",
        PropertyList({Property("destination", kPropertyTypeString, std::string("")),
                      Property("origin", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ReturnValue {
            const Config config = GetConfig();
            if (config.key.empty()) {
                if (setup_viewer_) {
                    setup_viewer_();
                }
                return std::string(
                    "Chưa có key TomTom. Bảo người dùng đăng ký miễn phí ở developer.tomtom.com rồi dán "
                    "key vào trang cài đặt (đã hiện mã QR trên màn hình robot).");
            }
            auto resolve = [&config](const std::string& text, double& lat, double& lon, std::string& label,
                                     std::string& error) {
                if (const Place* place = FindPlace(config.places, text)) {
                    lat = place->lat;
                    lon = place->lon;
                    label = place->name;
                    return true;
                }
                label = text;
                return ParseLatLon(text, lat, lon) || Geocode(config.key, text, lat, lon, error);
            };
            const std::string destination = Trim(properties["destination"].value<std::string>());
            const std::string origin = Trim(properties["origin"].value<std::string>());
            double from_lat = 0, from_lon = 0;
            std::string from_label, error;
            if (origin.empty()) {
                if (!CurrentOrigin(config, from_lat, from_lon, from_label)) {
                    return "Robot chưa biết đang ở đâu: chưa có GPS và chưa lưu địa điểm Nhà. Các địa "
                           "điểm đã lưu: " + PlaceNames(config.places) + ".";
                }
            } else if (!resolve(origin, from_lat, from_lon, from_label, error)) {
                return "Không tìm được điểm đi: " + error + ". Các địa điểm đã lưu: " + PlaceNames(config.places) + ".";
            }
            if (destination.empty()) {
                const Flow flow = GetFlow(config.key, from_lat, from_lon);
                if (!flow.ok) {
                    return "Không lấy được tình hình giao thông: " + flow.error + ".";
                }
                std::string text = "Đoạn đường ở " + from_label + ": xe đang chạy khoảng " +
                                   std::to_string(flow.current_kmh) + " km/h, lúc vắng khoảng " +
                                   std::to_string(flow.free_kmh) + " km/h";
                text += flow.closed ? ". Đường đang bị đóng."
                        : flow.current_kmh * 100 < flow.free_kmh * 45 ? ". Đang tắc."
                        : flow.current_kmh * 100 < flow.free_kmh * 75 ? ". Hơi đông." : ". Thông thoáng.";
                Notify(std::to_string(flow.current_kmh) + "/" + std::to_string(flow.free_kmh) + " km/h", 8000);
                return text;
            }
            double to_lat = 0, to_lon = 0;
            std::string to_label;
            if (!resolve(destination, to_lat, to_lon, to_label, error)) {
                return "Không tìm được nơi đến: " + error + ". Các địa điểm đã lưu: " + PlaceNames(config.places) + ".";
            }
            const Route route = GetRoute(config, from_lat, from_lon, to_lat, to_lon);
            if (!route.ok) {
                return "Không tính được đường: " + route.error + ".";
            }
            Notify(to_label + ": " + Minutes(route.time_s) + ", " + Km(route.length_m), 10000);
            return RouteSentence(from_label, to_label, route) +
                   " Robot không dẫn đường từng ngã rẽ; muốn chỉ đường thì dùng Google Maps trên điện thoại.";
        });

    mcp.AddTool(
        "self.nav.status",
        "Car mode status: GPS signal, current speed, speed limit, camera ahead, saved places, morning "
        "road check. Use when the user asks about GPS, their speed, saved places or the car settings.",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            const Config config = GetConfig();
            Status status;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status = last_status_;
            }
            std::string text = status.module_seen ? (status.has_fix ? "GPS tốt (" + std::to_string(status.satellites) +
                                                                          " vệ tinh). "
                                                                    : std::string("Module GPS đang dò vệ tinh. "))
                                                  : std::string("Chưa thấy module GPS. ");
            if (status.has_fix) {
                text += "Tốc độ " + std::to_string(status.speed_kmh) + " km/h. ";
            }
            text += status.limit_kmh > 0 ? "Giới hạn " + std::to_string(status.limit_kmh) + " km/h. "
                                         : std::string("Chưa đặt giới hạn tốc độ. ");
            if (status.camera_m >= 0) {
                text += "Camera phía trước " + std::to_string(status.camera_m) + " m. ";
            }
            text += "Địa điểm: " + PlaceNames(config.places) + ". ";
            text += std::to_string(config.cameras.size()) + " camera tự thêm, " +
                    std::to_string(nav_cameras::kBuiltInCount) + " camera có sẵn. ";
            if (config.am_minute >= 0) {
                char hhmm[24];
                snprintf(hhmm, sizeof(hhmm), "%02d:%02d", (config.am_minute / 60) % 24, config.am_minute % 60);
                text += "Báo đường buổi sáng lúc " + std::string(hhmm) + " từ " + config.am_from + " đến " +
                        config.am_to + ". ";
            }
            text += config.key.empty() ? "Chưa có key TomTom." : "Đã có key TomTom.";
            return text;
        });

    mcp.AddTool(
        "self.nav.camera",
        "Mark or remove a traffic/speed camera at the car's current GPS position. `action`=`add` when "
        "the user says đánh dấu camera ở đây / ở đây có camera (`limit_kmh` = the speed limit there if "
        "they say it, else 0); `remove` deletes the user's camera nearest to here (within 150 m).",
        PropertyList({Property("action", kPropertyTypeString, std::string("add")),
                      Property("limit_kmh", kPropertyTypeInteger, 0, 0, 150)}),
        [](const PropertyList& properties) -> ReturnValue {
            const GpsReader::Fix fix = GpsReader::GetInstance().GetFix();
            if (!fix.valid) {
                return std::string("Chưa có vị trí GPS nên chưa đánh dấu được.");
            }
            const std::string action = properties["action"].value<std::string>();
            std::lock_guard<std::mutex> lock(mutex_);
            if (action == "remove") {
                int best = -1;
                double best_m = 150;
                for (size_t i = 0; i < config_.cameras.size(); ++i) {
                    const double meters = DistanceMeters(fix.lat, fix.lon, config_.cameras[i].lat, config_.cameras[i].lon);
                    if (meters < best_m) {
                        best = static_cast<int>(i);
                        best_m = meters;
                    }
                }
                if (best < 0) {
                    return std::string("Không có camera tự thêm nào trong vòng 150 m.");
                }
                config_.cameras.erase(config_.cameras.begin() + best);
                SaveConfigLocked();
                return std::string("Đã xóa camera gần nhất.");
            }
            if (config_.cameras.size() >= kMaxUserCameras) {
                return std::string("Đã đủ 150 camera, xóa bớt trên trang cài đặt.");
            }
            UserCamera camera;
            camera.lat = fix.lat;
            camera.lon = fix.lon;
            camera.limit_kmh = properties["limit_kmh"].value<int>();
            config_.cameras.push_back(camera);
            SaveConfigLocked();
            return "Đã đánh dấu camera ở đây" +
                   (camera.limit_kmh > 0 ? ", tốc độ tối đa " + std::to_string(camera.limit_kmh) + " km/h" : std::string()) +
                   ". Lần sau đi qua robot sẽ nhắc trước.";
        });

    mcp.AddTool(
        "self.nav.place",
        "Save a named place for traffic questions (Nhà, Trường, Cơ quan...). `address` empty = the "
        "car's current GPS position; otherwise an address or 'lat, lon'.",
        PropertyList({Property("name", kPropertyTypeString), Property("address", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ReturnValue {
            const std::string name = Trim(properties["name"].value<std::string>());
            const std::string address = Trim(properties["address"].value<std::string>());
            if (name.empty() || name.find('|') != std::string::npos) {
                return std::string("Tên địa điểm không hợp lệ.");
            }
            Place place;
            place.name = name;
            const Config config = GetConfig();
            if (address.empty()) {
                const GpsReader::Fix fix = GpsReader::GetInstance().GetFix();
                if (!fix.valid) {
                    return std::string("Chưa có vị trí GPS. Hãy nói kèm địa chỉ, hoặc nhập trên trang cài đặt.");
                }
                place.lat = fix.lat;
                place.lon = fix.lon;
            } else {
                std::string error;
                if (!ParseLatLon(address, place.lat, place.lon) &&
                    !Geocode(config.key, address, place.lat, place.lon, error)) {
                    return "Không lưu được: " + error + ".";
                }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = config_.places.begin(); it != config_.places.end(); ++it) {
                if (NameKey(it->name) == NameKey(name)) {
                    config_.places.erase(it);
                    break;
                }
            }
            if (config_.places.size() >= kMaxPlaces) {
                return std::string("Đã đủ 12 địa điểm, xóa bớt trên trang cài đặt.");
            }
            config_.places.push_back(place);
            SaveConfigLocked();
            return "Đã lưu địa điểm " + name + ".";
        });

    mcp.AddTool(
        "self.nav.speed_limit",
        "Set the speed (km/h) above which the robot warns the driver; 0 turns the warning off.",
        PropertyList({Property("limit_kmh", kPropertyTypeInteger, 0, 150)}),
        [](const PropertyList& properties) -> ReturnValue {
            const int limit = properties["limit_kmh"].value<int>();
            std::lock_guard<std::mutex> lock(mutex_);
            config_.limit_kmh = limit;
            SaveConfigLocked();
            return limit > 0 ? "Robot sẽ nhắc khi chạy quá " + std::to_string(limit) + " km/h."
                             : std::string("Đã tắt nhắc tốc độ (vẫn nhắc ở camera có ghi tốc độ).");
        });

    mcp.AddTool(
        "self.nav.drive_screen",
        "Open or close the driving screen on the robot (big speed, speed limit, camera countdown, "
        "and a moving TomTom map). `page`: `speed`, `map` (bản đồ) or `close`. Use when the user "
        "asks for the driving screen, the map, màn hình lái xe, mở bản đồ. Tapping the screen "
        "switches between speed and map.",
        PropertyList({Property("page", kPropertyTypeString, std::string("speed"))}),
        [](const PropertyList& properties) -> ReturnValue {
            const std::string page = properties["page"].value<std::string>();
            if (!drive_viewer_) {
                return std::string("Robot này không có màn hình lái xe.");
            }
            if (page == "close") {
                drive_viewer_(0);
                return std::string("Đã đóng màn hình lái xe.");
            }
            drive_viewer_(page == "map" ? 2 : 1);
            std::string text = page == "map" ? "Đã mở bản đồ." : "Đã mở màn hình lái xe.";
            if (!GpsReader::GetInstance().ModuleSeen()) {
                text += " Chưa có GPS nên bản đồ hiện quanh địa điểm Nhà.";
            }
            if (page == "map" && GetConfig().key.empty()) {
                text += " Cần key TomTom để tải bản đồ.";
            }
            return text + " Chạm màn hình để đổi giữa tốc độ và bản đồ.";
        });

    mcp.AddTool(
        "self.nav.setup",
        "Show the QR code of the car-mode settings page (TomTom key, places, cameras, morning road "
        "check). Use when the user wants to set up navigation/traffic/car mode (cài đặt dẫn đường).",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            const std::string url = SetupUrl();
            if (url.empty()) {
                return std::string("Robot chưa có Wi-Fi.");
            }
            if (setup_viewer_) {
                setup_viewer_();
            } else {
                Notify("Mở: " + url, 30000);
            }
            return "Đã hiện mã QR trang cài đặt (" + url + "). Bảo người dùng quét bằng điện thoại cùng Wi-Fi.";
        });
}

}  // namespace nav
