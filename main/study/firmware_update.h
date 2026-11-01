#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <string>

// Updates the firmware over Wi-Fi from the latest GitHub release of this project. The release
// workflow (.github/workflows/release-ota.yml) publishes version.txt and the app binary for
// each board; the device compares that version with its own and installs a newer one.
class FirmwareUpdate {
public:
    struct Status {
        bool ok = false;           // the release could be read
        bool newer = false;        // the release is newer than the running firmware
        std::string current;       // running version
        std::string latest;        // release version (empty when not read)
        std::string error;
    };

    static FirmwareUpdate& GetInstance();

    // Registers the self.firmware.github_update MCP tool. Call once from InitializeTools().
    void RegisterTool();

    // Reads the release version. Blocks for a few seconds; do not call from the LVGL task.
    Status Check();
    // Checks and, when newer, downloads, installs and reboots. Run it on the main task.
    // Shows what happens on the display; returns false when nothing was installed.
    bool CheckAndInstall();

private:
    FirmwareUpdate() = default;
};

#endif  // FIRMWARE_UPDATE_H
