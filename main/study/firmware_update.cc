#include "firmware_update.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_log.h>

#include <charconv>
#include <memory>
#include <vector>

#define TAG "FirmwareUpdate"

// GitHub repository whose releases carry the firmware ("owner/name").
#ifndef FIRMWARE_UPDATE_REPO
#define FIRMWARE_UPDATE_REPO "Dangnguyen3368/xiaozhi-esp32-main"
#endif

namespace {
constexpr int kMaxRedirects = 5;

std::string ReleaseUrl(const std::string& file) {
    return std::string("https://github.com/") + FIRMWARE_UPDATE_REPO + "/releases/latest/download/" + file;
}

std::string BinaryName() {
    // The workflow names the binary after the board type, BOARD_TYPE.
    return std::string("xiaozhi-") + BOARD_TYPE + ".bin";
}

std::vector<int> ParseVersion(const std::string& version) {
    std::vector<int> parts;
    size_t start = 0;
    while (start <= version.size()) {
        size_t end = version.find('.', start);
        if (end == std::string::npos) {
            end = version.size();
        }
        int value = 0;
        auto [ptr, ec] = std::from_chars(version.data() + start, version.data() + end, value);
        if (end == start || ec != std::errc() || ptr != version.data() + end) {
            return {};
        }
        parts.push_back(value);
        start = end + 1;
    }
    return parts;
}

bool IsNewer(const std::string& current, const std::string& latest) {
    const auto a = ParseVersion(current);
    const auto b = ParseVersion(latest);
    if (a.empty() || b.empty()) {
        return false;
    }
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (b[i] != a[i]) {
            return b[i] > a[i];
        }
    }
    return b.size() > a.size();
}

std::string Trim(std::string text) {
    const char* space = " \t\r\n";
    const auto first = text.find_first_not_of(space);
    if (first == std::string::npos) {
        return "";
    }
    text.erase(text.find_last_not_of(space) + 1);
    return text.substr(first);
}

// Opens `url` with GET and follows redirects (GitHub sends release files from another host).
// On success `http` is open on the final response and `final_url` names it.
bool OpenFollowingRedirects(const std::string& url, std::unique_ptr<Http>& http, std::string& final_url,
                            std::string& error) {
    final_url = url;
    for (int i = 0; i <= kMaxRedirects; ++i) {
        http = Board::GetInstance().GetNetwork()->CreateHttp(0);
        http->SetTimeout(15000);
        if (auto opened = http->Open("GET", final_url); !opened) {
            error = "cannot open " + final_url + ": " + opened.error().ToString();
            return false;
        }
        auto status = http->GetStatusCode();
        if (!status) {
            error = "no HTTP status: " + status.error().ToString();
            http->Close();
            return false;
        }
        if (*status == 200) {
            return true;
        }
        if (*status == 301 || *status == 302 || *status == 303 || *status == 307 || *status == 308) {
            std::string location = http->GetResponseHeader("Location");
            http->Close();
            if (location.empty()) {
                error = "redirect without Location";
                return false;
            }
            final_url = location;
            continue;
        }
        error = "HTTP status " + std::to_string(*status) + " for " + final_url;
        http->Close();
        return false;
    }
    error = "too many redirects";
    return false;
}
}  // namespace

FirmwareUpdate& FirmwareUpdate::GetInstance() {
    static FirmwareUpdate instance;
    return instance;
}

FirmwareUpdate::Status FirmwareUpdate::Check() {
    Status status;
    status.current = esp_app_get_description()->version;
    std::unique_ptr<Http> http;
    std::string final_url;
    if (!OpenFollowingRedirects(ReleaseUrl("version.txt"), http, final_url, status.error)) {
        ESP_LOGW(TAG, "Check failed: %s", status.error.c_str());
        return status;
    }
    status.latest = Trim(http->ReadAll());
    http->Close();
    if (ParseVersion(status.latest).empty()) {
        status.error = "invalid version \"" + status.latest + "\"";
        return status;
    }
    status.ok = true;
    status.newer = IsNewer(status.current, status.latest);
    ESP_LOGI(TAG, "Running %s, latest release %s%s", status.current.c_str(), status.latest.c_str(),
             status.newer ? " (newer)" : "");
    return status;
}

bool FirmwareUpdate::CheckAndInstall() {
    auto* display = Board::GetInstance().GetDisplay();
    auto notify = [display](const std::string& text) {
        if (display != nullptr) {
            display->ShowNotification(text, 6000);
        }
    };
    notify("Đang kiểm tra bản mới...");
    const auto status = Check();
    if (!status.ok) {
        notify("Không kiểm tra được bản mới");
        return false;
    }
    if (!status.newer) {
        notify("Đang dùng bản mới nhất " + status.current);
        return false;
    }
    // Resolve the redirect here so the download itself is a plain 200 response.
    std::unique_ptr<Http> http;
    std::string final_url;
    std::string error;
    if (!OpenFollowingRedirects(ReleaseUrl(BinaryName()), http, final_url, error)) {
        ESP_LOGE(TAG, "Firmware not found: %s", error.c_str());
        notify("Không tải được firmware mới");
        return false;
    }
    http->Close();
    return Application::GetInstance().UpgradeFirmware(final_url, status.latest);
}

void FirmwareUpdate::RegisterTool() {
    McpServer::GetInstance().AddTool(
        "self.firmware.github_update",
        "Check for or install a new firmware version of this robot (cập nhật phần mềm / nâng cấp "
        "firmware) from its GitHub releases. Use action `check` first and tell the user the result; "
        "use `install` only when the user confirms they want to update. Installing downloads the "
        "firmware, then the robot reboots by itself (about a minute).",
        PropertyList({Property("action", kPropertyTypeString, std::string("check"))}),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto action = properties["action"].value<std::string>();
            if (action == "install") {
                auto& app = Application::GetInstance();
                app.Schedule([this]() { CheckAndInstall(); });
                return std::string("Checking and installing the latest firmware; the robot reboots when done.");
            }
            const auto status = Check();
            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "current_version", status.current.c_str());
            if (!status.ok) {
                cJSON_AddStringToObject(json, "error", ("Could not read the latest release: " + status.error).c_str());
            } else {
                cJSON_AddStringToObject(json, "latest_version", status.latest.c_str());
                cJSON_AddBoolToObject(json, "update_available", status.newer);
            }
            return json;
        });
}
