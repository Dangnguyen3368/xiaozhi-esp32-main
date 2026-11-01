#pragma once

#include <mutex>
#include <string>

// Voice control of the user's computer through tools/music_server/robot_may_tinh.py: type
// dictated text (Word, Excel, ...), press keys and open a few apps. The computer only accepts
// a robot paired with the code shown in its window.
class PcControl {
public:
    static PcControl& GetInstance();
    // Adds the self.pc.control MCP tool.
    void RegisterTool();

    // Runs `action` (pair, type, key, open) with `text` (the pairing code for pair). Blocks for up
    // to a few seconds; returns a short result, starting with "ERROR:" on failure.
    std::string Run(const std::string& action, const std::string& text);
    // Same, in a background task, showing the result as a screen notification (touch buttons).
    void RunAsync(const std::string& action, const std::string& text);
    bool IsPaired() const;

private:
    PcControl() = default;
    // POSTs `body` (JSON) to `path`; returns the computer's reply, or an error starting "ERROR:".
    std::string Post(const std::string& path, const std::string& body);
    std::string PostOnce(const std::string& base_url, const std::string& path, const std::string& body,
                         bool& connected);

    std::string base_url_;
    std::mutex mutex_;  // one request at a time (tool and touch buttons)
};
