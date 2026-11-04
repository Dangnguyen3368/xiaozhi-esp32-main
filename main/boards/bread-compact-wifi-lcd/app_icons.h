#ifndef APP_ICONS_H
#define APP_ICONS_H

#include <lvgl.h>

// 30 px Material Symbols Rounded glyphs for the app grid and the home screen dock.
// Regenerate font_app_icons_30.c with tools/app_icons/README.md after adding an icon here.
LV_FONT_DECLARE(font_app_icons_30);

namespace app_icon {
constexpr const char* kChat = "\xEE\x83\x89";          // U+E0C9 chat
constexpr const char* kSchool = "\xEE\xA0\x8C";        // U+E80C school
constexpr const char* kReplay = "\xEE\x81\x82";        // U+E042 replay
constexpr const char* kFunctions = "\xEE\x89\x8A";     // U+E24A functions
constexpr const char* kMonitoring = "\xEF\x86\x90";    // U+F190 monitoring
constexpr const char* kTimer = "\xEE\x90\xA5";         // U+E425 timer
constexpr const char* kCamera = "\xEE\x90\x92";        // U+E412 photo_camera
constexpr const char* kRadio = "\xEE\x80\xBE";         // U+E03E radio
constexpr const char* kLibraryMusic = "\xEE\x80\xB0";  // U+E030 library_music
constexpr const char* kComputer = "\xEE\x8C\x9E";      // U+E31E computer
constexpr const char* kBank = "\xEE\xA1\x8F";          // U+E84F account_balance
constexpr const char* kCar = "\xEE\xBF\xB7";           // U+EFF7 directions_car
constexpr const char* kCalendar = "\xEE\xAF\x8C";      // U+EBCC calendar_month
constexpr const char* kWeather = "\xEF\x85\xB2";       // U+F172 partly_cloudy_day
constexpr const char* kWifi = "\xEE\x98\xBE";          // U+E63E wifi
constexpr const char* kTune = "\xEE\x90\xA9";          // U+E429 tune
constexpr const char* kInfo = "\xEE\xA2\x8E";          // U+E88E info
constexpr const char* kUpdate = "\xEF\x8B\x8D";        // U+F2CD system_update
constexpr const char* kApps = "\xEE\x97\x83";          // U+E5C3 apps
constexpr const char* kAlarm = "\xEE\xA1\x95";         // U+E855 alarm
}  // namespace app_icon

#endif  // APP_ICONS_H
