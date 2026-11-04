#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "mochi_display.h"
#include "settings.h"
#include "system_reset.h"
#include "application.h"
#include "audio/internet_radio.h"
#include "pc/pc_control.h"
#include "pc/photo_solver.h"
#include "pc/bank_notify.h"
#include "study/physics_tutor.h"
#include "study/study_timer.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_system.h>
#include <array>
#include <memory>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    MochiDisplay* mochi_display_ = nullptr;
    std::unique_ptr<InternetRadio> internet_radio_;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        Settings settings("board", false);
        const std::string face = settings.GetString("face", "default");
        ESP_LOGI(TAG, "Loading display face setting: %s", face.c_str());
        if (face == "mochi") {
            mochi_display_ = new MochiDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                               DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
            display_ = mochi_display_;
        } else {
            display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                       DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        }
    }

    void InitializeButtons() {
        boot_button_.OnPressDown([this]() {
            if (internet_radio_ && internet_radio_->IsRunning()) {
                ESP_LOGI(TAG, "BOOT pressed: stopping Internet radio");
                internet_radio_->Stop();
                return;
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);

        Application::GetInstance().SetRadioStopCallback([this]() {
            if (internet_radio_) {
                internet_radio_->Stop();
            }
        });

        internet_radio_ = std::make_unique<InternetRadio>(
            GetAudioCodec(), [this](bool active, const std::array<uint8_t, 8>& levels) {
                // Works for both faces: Mochi shows full-screen bars, default shows them
                // in place of the emoji.
                if (display_ == nullptr) {
                    return;
                }
                display_->SetMusicVisualizer(active, internet_radio_->station_name());
                if (active) {
                    display_->SetMusicLevels(levels);
                }
            });

        McpServer::GetInstance().AddTool(
            "self.radio.internet",
            "Play or stop Internet radio on the device speaker. Only use when the user explicitly "
            "asks to listen to radio or music radio. Stations (music only): chill (Groove Salad, "
            "downtempo), ambient (Drone Zone, calm ambient), space (Deep Space One, ambient "
            "electronic), local (Nhạc máy tính: the user's own songs, including Vietnamese music, "
            "played from their computer; use it when they ask for their own or Vietnamese music). "
            "Use action `play` with a station id to start it, `next` to skip to the next song "
            "(local only) and `stop` to stop. "
            "Mochi shows live audio bars.",
            PropertyList({Property("action", kPropertyTypeString),
                          Property("station", kPropertyTypeString, std::string("chill"))}),
            [this](const PropertyList& properties) -> ReturnValue {
                const auto action = properties["action"].value<std::string>();
                if (action == "play") {
                    const auto station = properties["station"].value<std::string>();
                    if (InternetRadio::FindStation(station) == nullptr) {
                        return "Unknown station. Available: " + InternetRadio::StationList() + ".";
                    }
                    if (!internet_radio_->Start(station.c_str())) {
                        return std::string("Could not start Internet radio. Check Wi-Fi and try again.");
                    }
                    return "Starting " + std::string(internet_radio_->station_name()) + " on the device speaker.";
                }
                if (action == "next") {
                    if (!internet_radio_->NextTrack()) {
                        return std::string("Skipping only works while playing music from the computer (station local).");
                    }
                    return std::string("Skipped to the next song.");
                }
                if (action == "stop") {
                    internet_radio_->Stop();
                    return std::string("Stopping Internet radio.");
                }
                return std::string("Use action 'play', 'next' or 'stop'.");
            });
        PcControl::GetInstance().RegisterTool();
        photo_solver::RegisterTool();
        bank_notify::Start();

        McpServer::GetInstance().AddTool(
            "self.screen.switch_face",
            "Change the screen face only when the user clearly asks to switch it. Use `mochi` "
            "for the animated Mochi face with two square eyes, or `default` to restore the "
            "normal XiaoZhi screen. The selected face is saved and the device restarts to apply it.",
            PropertyList({Property("face", kPropertyTypeString)}),
            [](const PropertyList& properties) -> ReturnValue {
                const auto face = properties["face"].value<std::string>();
                if (face != "default" && face != "mochi") {
                    return std::string("Unknown face. Choose 'default' or 'mochi'.");
                }

                Settings settings("board", true);
                settings.SetString("face", face);
                ESP_LOGI(TAG, "Saved display face setting: %s; reboot scheduled", face.c_str());
                xTaskCreate(
                    [](void*) {
                        vTaskDelay(pdMS_TO_TICKS(1500));
                        esp_restart();
                    },
                    "restart_delay", 2048, nullptr, 5, nullptr);
                return std::string("Saved. The display will restart in ") + face + " mode.";
            });

        PhysicsTutor::GetInstance().RegisterTools();

        // Pomodoro study timer; its calm music uses the radio.
        auto& timer = StudyTimer::GetInstance();
        timer.SetMusicControl([this](bool on) {
            if (!internet_radio_) {
                return;
            }
            if (on) {
                internet_radio_->Start("ambient");
            } else {
                internet_radio_->Stop();
            }
        });
        timer.RegisterTool();
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
