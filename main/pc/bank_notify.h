#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Bank transfer announcer ("loa báo chuyển khoản"): an app on the phone (MacroDroid on Android)
// forwards each bank notification to http://<robot>:8080/bank?key=<key> on the home Wi-Fi. The
// robot shows it on screen and, for money received, reads the amount aloud
// ("Đã nhận năm trăm nghìn đồng") with the clips in main/assets/money_vi.
namespace bank_notify {

// Starts the robot's web page as soon as Wi-Fi is up and adds the MCP tools.
void Start();

// Shows a notification on screen (title, full text). Called from a background task.
void SetViewer(std::function<void(const std::string& title, const std::string& text)> viewer);
// Shows the setup page with the QR code (called on the main task from the voice tool).
void SetSetupViewer(std::function<void()> viewer);

// Address of the phone setup page with the secret key; empty without Wi-Fi.
std::string SetupUrl();
// Handles a notification text as if the phone had sent it (the menu's test button).
void Announce(const std::string& text);

// Exposed for tests. Amount of money received in `text` (0 when none, or when it is a payment
// out), parsed from "+500,000 VND", "nhận được 50.000đ", ...
uint64_t ParseIncomingAmount(const std::string& text);
// Clip names that read `amount` in Vietnamese ("nam", "tram", "nghin"). Empty above 999 tỷ.
std::vector<const char*> AmountWords(uint64_t amount);

}  // namespace bank_notify
