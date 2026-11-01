#pragma once

#include <atomic>
#include <string>

// Finds the companion program on the user's computer (tools/music_server/robot_may_tinh.py),
// which broadcasts "XIAOZHI_MUSIC <http port>" on UDP 8766 every 2 seconds on the local Wi-Fi.
namespace local_pc {

// Listens up to `seconds` for the announcement and returns "http://<ip>:<port>". Falls back to
// the last address that worked (kept in NVS). Empty when nothing is known. `cancel` may be null.
std::string Find(int seconds = 8, const std::atomic<bool>* cancel = nullptr);

// The last address found, without listening. Empty when none.
std::string Cached();

}  // namespace local_pc
