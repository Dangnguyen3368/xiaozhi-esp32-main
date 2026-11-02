#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "audio/internet_radio.h"
#include "pc/pc_control.h"
#include "pc/photo_solver.h"
#include "study/physics_tutor.h"
#include "xpt2046_touch.h"
#include "figure_view.h"
#include "study_ui.h"
#include "wifi_setup_view.h"
#include "study/study_timer.h"
#include "study/firmware_update.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <array>
#include <memory>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardLCD"

class CompactWifiBoardLCD : public WifiBoard {
private:
 
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* display_;
    std::unique_ptr<Xpt2046Touch> touch_;
    std::unique_ptr<InternetRadio> internet_radio_;
    FigureView figure_view_;
    std::unique_ptr<StudyMenu> study_menu_;
    ProgressView progress_view_;
    PomodoroBadge pomodoro_badge_;
    MenuButton menu_button_;
    std::unique_ptr<WifiSetupView> wifi_setup_;

    bool AnyOverlayShown() const {
        return figure_view_.IsShown() || progress_view_.IsShown() || (study_menu_ && study_menu_->IsShown()) ||
               (wifi_setup_ && wifi_setup_->IsShown());
    }

    // The server ignores text sent as a wake phrase in the middle of a chat, so the problem is
    // picked and shown here; the assistant is only told its id so it can help with it.
    static void ShowProblemAndTell(bool review) {
        const auto id = PhysicsTutor::GetInstance().PickOnScreen(review);
        if (!id.empty()) {
            Application::GetInstance().AskAssistant("Em đang làm bài " + id);
        }
    }

    void CreateStudyMenu() {
        StudyMenu::Actions actions;
        actions.practice = []() { ShowProblemAndTell(false); };
        actions.review = []() { ShowProblemAndTell(true); };
        actions.toggle_pomodoro = []() {
            auto& timer = StudyTimer::GetInstance();
            if (timer.GetState().running) {
                timer.Stop();
            } else {
                timer.Start(25, 5, true);
            }
        };
        actions.formulas = [](const std::string& topic) { PhysicsTutor::GetInstance().ShowFormulas(topic); };
        actions.progress = []() { PhysicsTutor::GetInstance().ShowProgress(); };
        actions.toggle_radio = [this]() {
            if (!internet_radio_) {
                return;
            }
            if (internet_radio_->IsRunning()) {
                internet_radio_->Stop();
            } else {
                internet_radio_->Start("chill");
            }
        };
        actions.local_music = [this]() {
            if (!internet_radio_) {
                return;
            }
            if (internet_radio_->IsLocal()) {
                internet_radio_->NextTrack();
            } else {
                internet_radio_->Start(InternetRadio::kLocalStation);
            }
        };
        actions.update_firmware = []() { FirmwareUpdate::GetInstance().CheckAndInstall(); };
        actions.wifi_setup = [this]() { wifi_setup_->Start(); };
        actions.pc_command = [](const std::string& action, const std::string& text) {
            PcControl::GetInstance().RunAsync(action, text);
        };
        actions.pc_paired = []() { return PcControl::GetInstance().IsPaired(); };
        actions.phone_photo_url = []() { return photo_solver::PhoneUrl(); };
        actions.start_dictation = []() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.ToggleChatState();
            }
        };
        actions.pomodoro_running = []() { return StudyTimer::GetInstance().GetState().running; };
        actions.radio_running = [this]() { return internet_radio_ && internet_radio_->IsRunning(); };
        actions.local_music_running = [this]() { return internet_radio_ && internet_radio_->IsLocal(); };
        study_menu_ = std::make_unique<StudyMenu>(std::move(actions));
        wifi_setup_ = std::make_unique<WifiSetupView>([this]() { EnterWifiConfigMode(); });
    }

    // Same action as the BOOT button: stop the radio if it plays, otherwise toggle chat.
    void ToggleChatOrStopRadio() {
        if (internet_radio_ && internet_radio_->IsRunning()) {
            ESP_LOGI(TAG, "Stopping Internet radio");
            internet_radio_->Stop();
            return;
        }
        Application::GetInstance().ToggleChatState();
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        const Xpt2046Touch::Calibration calibration = {
            TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX,
            TOUCH_SWAP_XY, TOUCH_MIRROR_X, TOUCH_MIRROR_Y,
        };
        touch_ = std::make_unique<Xpt2046Touch>(SPI3_HOST, TOUCH_CS_PIN, TOUCH_IRQ_PIN,
                                                DISPLAY_WIDTH, DISPLAY_HEIGHT, calibration);
        // While a study screen is open its own buttons handle touches. Otherwise a tap works like
        // the BOOT button and a long press opens the study menu.
        touch_->OnTap([this](int x, int y) {
            // The Menu button handles its own taps.
            if (AnyOverlayShown() || MenuButton::Contains(x, y, DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
                return;
            }
            Application::GetInstance().Schedule([this]() { ToggleChatOrStopRadio(); });
        });
        touch_->OnLongPress([this]() {
            if (AnyOverlayShown() || !study_menu_) {
                return;
            }
            Application::GetInstance().Schedule([this]() { study_menu_->Show(); });
        });
        DisplayLockGuard lock(display_);
        touch_->AttachToLvgl();
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            ToggleChatOrStopRadio();
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

    // 物联网初始化，添加对 AI 可见设备
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);

        Application::GetInstance().SetRadioStopCallback([this]() {
            if (internet_radio_) {
                internet_radio_->Stop();
            }
        });

        internet_radio_ = std::make_unique<InternetRadio>(
            GetAudioCodec(), [this](bool active, const std::array<uint8_t, 8>& levels) {
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
            "(local only) and `stop` to stop.",
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

        auto& tutor = PhysicsTutor::GetInstance();
        tutor.SetFigureViewer(
            [this](const PhysicsFigure* figure, const std::string& caption, PhysicsTutor::ScreenKind kind) {
                figure_view_.Show(figure, caption, kind == PhysicsTutor::ScreenKind::kProblem);
            });
        tutor.SetProgressViewer([this](const PhysicsTutor::ProgressReport& report) { progress_view_.Show(report); });
        // Hint and solution come straight from the bank, so they work whatever the assistant
        // is doing. Only "next problem" needs the assistant, which keeps track of the session.
        figure_view_.SetProblemActions({
            [this]() {
                std::string title, text;
                if (PhysicsTutor::GetInstance().NextHintOnScreen(title, text)) {
                    figure_view_.ShowNote(title, text);
                }
            },
            [this]() {
                std::string title, text;
                if (PhysicsTutor::GetInstance().SolutionOnScreen(title, text)) {
                    figure_view_.ShowNote(title, text);
                }
            },
            []() { ShowProblemAndTell(false); },
        });
        tutor.RegisterTools();

        auto& timer = StudyTimer::GetInstance();
        timer.SetListener([this](const StudyTimer::State& state) { pomodoro_badge_.Update(state); });
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
        FirmwareUpdate::GetInstance().RegisterTool();
        PcControl::GetInstance().RegisterTool();
        photo_solver::RegisterTool();
        photo_solver::SetSolutionViewer([this](const std::string& text) {
            if (study_menu_) {
                study_menu_->Hide();
            }
            figure_view_.ShowNote("Lời giải ảnh vừa chụp", text);
        });
        CreateStudyMenu();
        // Created once the main loop runs: building the button looks up the theme font through
        // Board::GetInstance(), which must not be called while the board is still being built.
        Application::GetInstance().Schedule([this]() {
            DisplayLockGuard lock(display_);
            menu_button_.Create([this]() {
                if (!AnyOverlayShown()) {
                    study_menu_->Show();
                }
            });
        });
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
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

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);