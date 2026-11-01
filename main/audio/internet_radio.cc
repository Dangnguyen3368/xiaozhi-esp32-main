#include "internet_radio.h"

#include "audio_codec.h"
#include "application.h"

#include <esp_audio_types.h>
#include <esp_ae_rate_cvt.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mp3_dec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
constexpr char kTag[] = "InternetRadio";
// Music-only MP3 streams from SomaFM (listener-supported, no news or talk).
constexpr InternetRadio::Station kStations[] = {
    {"chill", "Groove Salad", "https://ice1.somafm.com/groovesalad-128-mp3"},
    {"ambient", "Drone Zone", "https://ice1.somafm.com/dronezone-128-mp3"},
    {"space", "Deep Space One", "https://ice1.somafm.com/deepspaceone-128-mp3"},
};
constexpr size_t kInputBufferSize = 12 * 1024;
constexpr size_t kReadBufferSize = 2048;
constexpr size_t kDecodedBufferSize = 16 * 1024;
// Redraw the OLED bars at most 10 times per second so I2C flushes do not starve playback.
constexpr TickType_t kVisualizerInterval = pdMS_TO_TICKS(100);

esp_ae_rate_cvt_cfg_t MakeRateConfig(uint32_t source_rate, uint32_t target_rate) {
    return esp_ae_rate_cvt_cfg_t{
        .src_rate = source_rate,
        .dest_rate = target_rate,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .complexity = 2,
        .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
    };
}

bool OpenRadioStream(esp_http_client_handle_t client) {
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGW(kTag, "Radio connection failed; will retry");
        esp_http_client_close(client);
        return false;
    }

    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGW(kTag, "Radio server returned HTTP %d; will retry", status);
        esp_http_client_close(client);
        return false;
    }
    return true;
}
}  // namespace

InternetRadio::InternetRadio(AudioCodec* codec, VisualizerCallback visualizer_callback)
    : codec_(codec), visualizer_callback_(std::move(visualizer_callback)) {}

const InternetRadio::Station* InternetRadio::FindStation(const std::string& id) {
    for (const auto& station : kStations) {
        if (id == station.id) {
            return &station;
        }
    }
    return nullptr;
}

std::string InternetRadio::StationList() {
    std::string list;
    for (const auto& station : kStations) {
        if (!list.empty()) {
            list += ", ";
        }
        list += std::string(station.id) + " (" + station.name + ")";
    }
    return list;
}

bool InternetRadio::Start(const char* station) {
    const Station* found = station != nullptr ? FindStation(station) : nullptr;
    if (codec_ == nullptr || found == nullptr) {
        return false;
    }
    if (running_.load()) {
        if (stream_url_ == found->url) {
            return true;
        }
        // Switching station: let the old stream task finish (an HTTP read can block for up
        // to its 4 s timeout) before reusing the codec.
        stop_requested_ = true;
        for (int waited_ms = 0; running_.load() && waited_ms < 5000; waited_ms += 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (running_.load()) {
            return false;
        }
    }

    station_name_ = found->name;
    stream_url_ = found->url;
    stop_requested_ = false;
    running_ = true;
    Application::GetInstance().SetRadioPlaybackActive(true);
    if (xTaskCreate(TaskEntry, "internet_radio", 8192, this, 5, &task_handle_) != pdPASS) {
        task_handle_ = nullptr;
        running_ = false;
        Application::GetInstance().SetRadioPlaybackActive(false);
        return false;
    }
    return true;
}

void InternetRadio::Stop() {
    stop_requested_ = true;
}

bool InternetRadio::IsRunning() const {
    return running_.load();
}

void InternetRadio::TaskEntry(void* arg) {
    static_cast<InternetRadio*>(arg)->Run();
}

void InternetRadio::UpdateVisualizer(const int16_t* pcm, size_t samples) {
    if (!visualizer_callback_ || pcm == nullptr || samples == 0) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (now - last_visualizer_tick_ < kVisualizerInterval) {
        return;
    }
    last_visualizer_tick_ = now;

    std::array<uint8_t, 8> levels{};
    for (size_t band = 0; band < levels.size(); ++band) {
        const size_t begin = samples * band / levels.size();
        const size_t end = samples * (band + 1) / levels.size();
        if (end <= begin) {
            continue;
        }
        uint64_t square_sum = 0;
        for (size_t i = begin; i < end; ++i) {
            const int32_t value = pcm[i];
            square_sum += static_cast<uint64_t>(value * value);
        }
        const float rms = std::sqrt(static_cast<float>(square_sum) / (end - begin));
        levels[band] = static_cast<uint8_t>(std::clamp(rms * (100.0f / 9000.0f), 4.0f, 100.0f));
    }
    visualizer_callback_(true, levels);
}

void InternetRadio::Run() {
    auto& application = Application::GetInstance();
    auto& audio_service = application.GetAudioService();
    const TickType_t wait_started = xTaskGetTickCount();
    const TickType_t minimum_wait = pdMS_TO_TICKS(3000);
    const TickType_t quiet_period = pdMS_TO_TICKS(250);
    TickType_t quiet_started = 0;
    bool assistant_spoke = application.GetDeviceState() == kDeviceStateSpeaking;

    ESP_LOGI(kTag, "Waiting for the assistant's spoken reply to finish");
    while (!stop_requested_.load()) {
        const bool speaking = application.GetDeviceState() == kDeviceStateSpeaking;
        assistant_spoke = assistant_spoke || speaking;
        const TickType_t now = xTaskGetTickCount();
        const bool minimum_elapsed = now - wait_started >= minimum_wait;
        const bool playback_idle = audio_service.IsPlaybackIdle();

        if (minimum_elapsed && !speaking && playback_idle) {
            if (quiet_started == 0) {
                quiet_started = now;
            } else if (now - quiet_started >= quiet_period) {
                break;
            }
        } else {
            quiet_started = 0;
        }

        // If no spoken acknowledgement starts, avoid waiting forever.
        if (!assistant_spoke && now - wait_started >= pdMS_TO_TICKS(6000) && playback_idle) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (stop_requested_.load()) {
        Application::GetInstance().SetRadioPlaybackActive(false);
        task_handle_ = nullptr;
        running_ = false;
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag, "Assistant reply finished; connecting to radio stream");

    esp_http_client_config_t http_config = {
        .url = stream_url_,
        .timeout_ms = 4000,
        .buffer_size = kReadBufferSize,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    void* decoder = nullptr;
    esp_ae_rate_cvt_handle_t resampler = nullptr;
    std::vector<uint8_t> encoded(kInputBufferSize);
    std::vector<uint8_t> network_data(kReadBufferSize);
    std::vector<uint8_t> decoded(kDecodedBufferSize);
    // Reused across frames to avoid heap churn in the audio path.
    std::vector<int16_t> mono;
    std::vector<int16_t> converted;
    size_t encoded_size = 0;
    uint32_t resampler_source_rate = 0;
    const int output_rate = codec_->output_sample_rate();
    bool show_visualizer = false;

    if (client == nullptr) {
        ESP_LOGE(kTag, "Could not create HTTP client");
        goto cleanup;
    }
    esp_http_client_set_header(client, "Icy-MetaData", "0");
    esp_http_client_set_header(client, "User-Agent", "XiaoZhi-Internet-Radio/1.0");
    while (!stop_requested_.load() && !OpenRadioStream(client)) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (stop_requested_.load()) {
        goto cleanup;
    }
    if (esp_mp3_dec_open(nullptr, 0, &decoder) != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        ESP_LOGE(kTag, "Could not open MP3 decoder");
        goto cleanup;
    }

    codec_->EnableOutput(true);
    if (visualizer_callback_) {
        visualizer_callback_(true, {});
        show_visualizer = true;
    }
    ESP_LOGI(kTag, "Playing %s", station_name_);

    while (!stop_requested_.load()) {
        const int received = esp_http_client_read(client, reinterpret_cast<char*>(network_data.data()),
                                                  network_data.size());
        if (received < 0 ||
            (received == 0 && esp_http_client_is_complete_data_received(client))) {
            if (stop_requested_.load()) {
                break;
            }
            ESP_LOGW(kTag, "Radio stream interrupted; reconnecting");
            esp_http_client_close(client);
            encoded_size = 0;
            esp_mp3_dec_reset(decoder);
            bool reconnected = false;
            while (!stop_requested_.load() && !reconnected) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                reconnected = OpenRadioStream(client);
            }
            if (!reconnected) {
                break;
            }
            continue;
        }
        if (received == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (encoded_size + static_cast<size_t>(received) > encoded.size()) {
            const size_t keep = std::min(encoded_size, encoded.size() / 2);
            memmove(encoded.data(), encoded.data() + encoded_size - keep, keep);
            encoded_size = keep;
        }
        memcpy(encoded.data() + encoded_size, network_data.data(), received);
        encoded_size += received;

        while (encoded_size > 0 && !stop_requested_.load()) {
            esp_audio_dec_in_raw_t raw = {
                .buffer = encoded.data(),
                .len = static_cast<uint32_t>(encoded_size),
                .consumed = 0,
                .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
            };
            esp_audio_dec_out_frame_t frame = {
                .buffer = decoded.data(),
                .len = static_cast<uint32_t>(decoded.size()),
                .needed_size = 0,
                .decoded_size = 0,
            };
            esp_audio_dec_info_t info = {};
            const esp_audio_err_t result = esp_mp3_dec_decode(decoder, &raw, &frame, &info);

            if (raw.consumed > 0) {
                encoded_size -= raw.consumed;
                memmove(encoded.data(), encoded.data() + raw.consumed, encoded_size);
            }

            if (result == ESP_AUDIO_ERR_OK && frame.decoded_size >= sizeof(int16_t) &&
                info.sample_rate > 0 && info.channel > 0) {
                const size_t decoded_samples = frame.decoded_size / sizeof(int16_t);
                const size_t mono_samples_count = decoded_samples / info.channel;
                auto* decoded_pcm = reinterpret_cast<const int16_t*>(decoded.data());
                mono.resize(mono_samples_count);
                if (info.channel == 1) {
                    memcpy(mono.data(), decoded_pcm, mono_samples_count * sizeof(int16_t));
                } else {
                    for (size_t i = 0; i < mono_samples_count; ++i) {
                        mono[i] = static_cast<int16_t>((static_cast<int32_t>(decoded_pcm[i * 2]) +
                                                        decoded_pcm[i * 2 + 1]) / 2);
                    }
                }

                UpdateVisualizer(mono.data(), mono.size());
                if (info.sample_rate != static_cast<uint32_t>(output_rate)) {
                    if (resampler_source_rate != info.sample_rate) {
                        if (resampler != nullptr) {
                            esp_ae_rate_cvt_close(resampler);
                            resampler = nullptr;
                        }
                        auto config = MakeRateConfig(info.sample_rate, output_rate);
                        if (esp_ae_rate_cvt_open(&config, &resampler) != ESP_AE_ERR_OK) {
                            ESP_LOGE(kTag, "Could not resample radio audio (%lu Hz)",
                                     static_cast<unsigned long>(info.sample_rate));
                            stop_requested_ = true;
                            break;
                        }
                        resampler_source_rate = info.sample_rate;
                    }
                    uint32_t output_samples = 0;
                    esp_ae_rate_cvt_get_max_out_sample_num(resampler, mono.size(), &output_samples);
                    converted.resize(output_samples);
                    if (esp_ae_rate_cvt_process(resampler, mono.data(), mono.size(), converted.data(),
                                                &output_samples) == ESP_AE_ERR_OK) {
                        converted.resize(output_samples);
                        codec_->OutputData(converted);
                    }
                } else {
                    codec_->OutputData(mono);
                }
            } else if (result != ESP_AUDIO_ERR_DATA_LACK && result != ESP_AUDIO_ERR_CONTINUE) {
                // Skip bytes that are not part of an MP3 frame and resync.
                if (raw.consumed == 0 && encoded_size > 0) {
                    memmove(encoded.data(), encoded.data() + 1, --encoded_size);
                }
            }

            if (raw.consumed == 0 &&
                (result == ESP_AUDIO_ERR_DATA_LACK || result == ESP_AUDIO_ERR_CONTINUE)) {
                break;
            }
        }
    }

cleanup:
    if (resampler != nullptr) {
        esp_ae_rate_cvt_close(resampler);
    }
    if (decoder != nullptr) {
        esp_mp3_dec_close(decoder);
    }
    if (client != nullptr) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    // Do not disable codec output here. On duplex I2S the mic (RX) runs as slave of the
    // speaker (TX) clock, so turning TX off would silence the mic and the wake word until
    // reboot. AudioService's power timer turns output off safely once it is idle.
    if (show_visualizer && visualizer_callback_) {
        visualizer_callback_(false, {});
    }
    Application::GetInstance().SetRadioPlaybackActive(false);
    task_handle_ = nullptr;
    running_ = false;
    vTaskDelete(nullptr);
}
