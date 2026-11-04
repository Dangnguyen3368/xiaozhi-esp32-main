#pragma once

#include <driver/gpio.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Car mode: the robot rides in the car on the phone's Wi-Fi hotspot with a GPS module
// (GY-NEO6MV2) and
//  - warns before speed cameras (OpenStreetMap list + cameras the user marks by voice/web page),
//  - warns when driving faster than the road's speed limit (TomTom), a camera's limit or the
//    user's own maximum, whichever is lowest,
//  - warns about a traffic jam about 1 km ahead (TomTom traffic flow, needs a free API key),
//  - answers "how long to X / is the road jammed" by voice (TomTom routing with live traffic),
//  - checks the road to work/school every morning and says to leave early when it is slow.
// Settings live on the robot's web page http://<robot>:8081/xe (QR code on screen).
namespace nav {

struct Status {
    bool module_seen = false;  // NMEA data arrives from the GPS module
    bool has_fix = false;      // a position is known (module or phone)
    int satellites = 0;
    int speed_kmh = 0;
    int limit_kmh = 0;         // the limit the robot warns at (road, camera or the user's), 0 = none
    int road_limit_kmh = 0;    // the road's own limit from TomTom, 0 = unknown
    std::string road;          // street name from TomTom, empty = unknown
    int camera_m = -1;         // distance to the camera ahead, -1 = none near
    bool overspeed = false;
    double lat = 0;            // valid when has_fix
    double lon = 0;
    float course_deg = 0;
    std::string note;          // last traffic alert, kept 10 minutes
};

// Where the map should look: the GPS position, else the place "Nhà", else the first saved place,
// else Thanh Hóa city. `label` says which.
void MapCenter(double& lat, double& lon, std::string& label);
// Cameras (built-in and the user's) within `meters` of a point.
std::vector<std::pair<double, double>> CamerasNear(double lat, double lon, double meters);
// Downloads one TomTom raster map tile (PNG). False without a key or on a network error.
bool FetchMapTile(int zoom, int x, int y, std::string& png);
bool HasKey();

// Starts the GPS reader (`rx` = robot pin wired to the module's TX), the monitor task and the
// settings page.
void Start(gpio_num_t rx, gpio_num_t tx);
// Adds the self.nav.* MCP tools.
void RegisterTools();

// Called about once a second from the monitor task.
void SetStatusListener(std::function<void(const Status&)> listener);
// Opens the driving screen: 0 closes it, 1 shows the speed page, 2 the map page.
void SetDriveViewer(std::function<void(int page)> viewer);
// Shows the settings QR code (called when the voice tool or the web page needs it).
void SetSetupViewer(std::function<void()> viewer);
// False while the robot must not play alerts (the Internet radio writes to the speaker).
void SetCanSpeak(std::function<bool()> can_speak);

// "http://<robot ip>:8081/xe", or empty without Wi-Fi.
std::string SetupUrl();

}  // namespace nav
