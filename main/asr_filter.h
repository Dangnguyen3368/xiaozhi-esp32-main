#ifndef ASR_FILTER_H
#define ASR_FILTER_H

#include <cctype>
#include <string>

// The server's speech recognizer sometimes "hears" YouTube outro lines in
// silence or background noise (it was trained on video subtitles). Such text
// is never something the user said, so the robot drops it instead of
// answering or typing it on the computer.
inline bool IsAsrHallucination(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char c : text) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    static const char* const kPhrases[] = {
        "subscribe",
        "lalaschool",
        "la la school",
        "đăng ký kênh",
        "Đăng ký kênh",
        "ghiền mì gõ",
        "bỏ lỡ những video",
        "theo dõi kênh",
        "cảm ơn các bạn đã theo dõi",
        "cảm ơn các bạn đã xem",
        "hẹn gặp lại các bạn trong những video",
        "like và share",
        "amara.org",
    };
    for (const char* phrase : kPhrases) {
        if (lower.find(phrase) != std::string::npos) {
            return true;
        }
    }
    return false;
}

#endif  // ASR_FILTER_H
