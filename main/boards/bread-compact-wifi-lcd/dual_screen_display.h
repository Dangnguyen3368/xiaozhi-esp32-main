#pragma once

// TFT display that also drives an optional 1.3" SH1106 OLED showing the Mochi face.
// The TFT stays the main display (chat, menus, touch). Emotions and the music/radio bars are
// copied to the OLED, so Mochi reacts at the same time as the TFT.

#include "display/lcd_display.h"
#include "mochi_display.h"
#include "home_screen.h"

#include <array>

class DualScreenLcdDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    // Must be called before SetupUI(). nullptr = no OLED plugged in.
    void AttachMochi(MochiDisplay* mochi) { mochi_ = mochi; }
    MochiDisplay* mochi() const { return mochi_; }
    lv_display_t* main_lv_display() const { return display_; }

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        if (mochi_ != nullptr) {
            mochi_->SetupUI();
        }
    }

    void SetEmotion(const char* emotion) override {
        SpiLcdDisplay::SetEmotion(emotion);
        if (mochi_ != nullptr) {
            mochi_->SetEmotion(emotion);
        }
    }

    // A chat line or notification must be readable, so the idle home screen steps aside.
    void SetChatMessage(const char* role, const char* content) override {
        if (content != nullptr && content[0] != '\0') {
            HomeScreen::NoteActivity();
        }
        SpiLcdDisplay::SetChatMessage(role, content);
    }

    void ShowNotification(const char* notification, int duration_ms = 3000) override {
        HomeScreen::NoteActivity();
        SpiLcdDisplay::ShowNotification(notification, duration_ms);
    }

    void ShowNotification(const std::string& notification, int duration_ms = 3000) override {
        HomeScreen::NoteActivity();
        SpiLcdDisplay::ShowNotification(notification, duration_ms);
    }

    void SetMusicVisualizer(bool active, const char* title) override {
        HomeScreen::SetMusicActive(active);
        SpiLcdDisplay::SetMusicVisualizer(active, title);
        if (mochi_ != nullptr) {
            mochi_->SetMusicVisualizer(active, title);
        }
    }

    void SetMusicLevels(const std::array<uint8_t, 8>& levels) override {
        SpiLcdDisplay::SetMusicLevels(levels);
        if (mochi_ != nullptr) {
            mochi_->SetMusicLevels(levels);
        }
    }

private:
    MochiDisplay* mochi_ = nullptr;
};
