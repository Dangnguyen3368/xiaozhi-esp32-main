#include "duo_teach.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <cstring>

#define TAG "DuoTeach"

namespace {
constexpr uint16_t kPort = 39601;
constexpr char kMagic[] = "XZDUO ";
constexpr int64_t kSecond = 1000000;
constexpr int64_t kPeerTimeout = 8 * kSecond;      // No hello for this long: partner is gone
constexpr int64_t kHelloInterval = 2 * kSecond;
constexpr int64_t kQuietGrace = 5 * kSecond;       // Lost "end": partner quiet this long counts as done
constexpr int64_t kMaxListen = 120 * kSecond;      // Longest partner speech we record
constexpr int64_t kClaimWindow = 2 * kSecond;      // Both woken at once: the first robot keeps it
constexpr int kCopies = 3;                         // Wi-Fi broadcasts are not acknowledged
constexpr size_t kMaxText = 700;                   // Bytes of spoken text sent with "end"
constexpr int64_t kMicReplyWait = 7 * kSecond;     // No STT after the recording: send the text
constexpr int64_t kHeardReplyWait = 25 * kSecond;  // STT but no answer: give up
constexpr int64_t kTextReplyWait = 15 * kSecond;   // No answer to the text either: ask the student

// Cut at a UTF-8 character boundary.
std::string Truncate(const std::string& text, size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return text;
    }
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

int64_t Now() { return esp_timer_get_time(); }
}  // namespace

DuoTeach& DuoTeach::GetInstance() {
    static DuoTeach instance;
    return instance;
}

bool DuoTeach::PeerOnline() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !peer_id_.empty() && Now() - peer_seen_us_ < kPeerTimeout;
}

void DuoTeach::SaveSettings() {
    Settings settings("duo", true);
    settings.SetBool("on", enabled_.load());
    settings.SetInt("turns", max_turns_);
}

std::string DuoTeach::StatusText() const {
    std::string text = enabled_ ? "Chế độ hai robot cùng giảng đang BẬT" : "Chế độ hai robot cùng giảng đang TẮT";
    if (enabled_) {
        text += PeerOnline() ? "; đã thấy robot bạn trong cùng mạng Wi-Fi"
                             : "; chưa thấy robot bạn (robot kia cũng phải bật chế độ này và cùng mạng Wi-Fi)";
        text += "; hai robot nói qua lại tối đa " + std::to_string(max_turns_) +
                " lượt rồi mời học sinh nói.";
    }
    return text;
}

void DuoTeach::Start() {
    if (started_.exchange(true)) {
        return;
    }
    {
        Settings settings("duo");
        enabled_ = settings.GetBool("on", false);
        max_turns_ = std::clamp<int>(settings.GetInt("turns", 4), 2, 10);
    }
    my_id_ = SystemInfo::GetMacAddress();
    my_boot_ = esp_random();

    McpServer::GetInstance().AddTool(
        "self.duo.mode",
        "Two robots teaching together (hai robot cùng giảng bài). When on, this robot and a second "
        "robot on the same Wi-Fi take turns: while one speaks the other listens, then answers or adds "
        "to what its partner said; after `turns` replies between them, the robot asks the student. "
        "Each robot must be switched on separately. Actions: `on`, `off`, `status`, `partner_said`. "
        "`turns` (2-10) changes the number of replies in a row between the robots; 0 keeps it. "
        "IMPORTANT: when the user message is exactly \"" DUO_CUE "\", your partner robot has just "
        "finished speaking: call this tool with action `partner_said` to get its exact words, then "
        "continue the lesson from them (answer, add to, or question what it said). Never answer "
        "\"" DUO_CUE "\" without calling the tool first.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("turns", kPropertyTypeInteger, 0, 0, 10)}),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto action = properties["action"].value<std::string>();
            const int turns = properties["turns"].value<int>();
            if (turns >= 2) {
                max_turns_ = turns;
            }
            if (action == "partner_said") {
                std::lock_guard<std::mutex> lock(mutex_);
                if (peer_text_.empty()) {
                    return std::string("Robot bạn chưa nói gì (hoặc chưa gửi được chữ). Hãy hỏi học sinh.");
                }
                return "Robot bạn vừa nói (lượt " + std::to_string(peer_turn_) + "/" +
                       std::to_string(max_turns_) + "): " + peer_text_;
            }
            if (action == "on") {
                enabled_ = true;
            } else if (action == "off") {
                enabled_ = false;
            } else if (action != "status") {
                return std::string("Dùng action on, off, status hoặc partner_said.");
            }
            SaveSettings();
            ESP_LOGI(TAG, "Duo mode %s, %d turns", enabled_ ? "on" : "off", max_turns_);
            return StatusText();
        });

    xTaskCreate([](void* arg) { static_cast<DuoTeach*>(arg)->Task(); }, "duo_teach", 4096, this, 3,
                nullptr);
}

void DuoTeach::Broadcast(const std::string& event, int turn, bool final_turn, const std::string& text) {
    // Caller holds mutex_.
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", my_id_.c_str());
    cJSON_AddNumberToObject(json, "boot", my_boot_);
    cJSON_AddNumberToObject(json, "seq", ++seq_);
    cJSON_AddStringToObject(json, "ev", event.c_str());
    cJSON_AddNumberToObject(json, "turn", turn);
    cJSON_AddBoolToObject(json, "final", final_turn);
    cJSON_AddBoolToObject(json, "spk", speaking_ || end_pending_);
    if (!text.empty()) {
        cJSON_AddStringToObject(json, "text", text.c_str());
    }
    char* printed = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (printed == nullptr) {
        return;
    }
    outbox_.push_back({std::string(kMagic) + printed, event == "hi" ? 1 : kCopies});
    cJSON_free(printed);
    ESP_LOGI(TAG, "-> %s turn %d%s", event.c_str(), turn, final_turn ? " (last)" : "");
}

void DuoTeach::OnSpeakStart(bool reply_to_peer) {
    if (!enabled_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    speaking_ = true;
    end_pending_ = false;
    reply_stage_ = 0;
    spoken_text_.clear();
    my_turn_ = reply_to_peer ? peer_turn_ + 1 : 1;
    Broadcast("start", my_turn_);
}

DuoTeach::AfterSpeech DuoTeach::OnSpeakStop() {
    if (!enabled_) {
        return AfterSpeech::kNormal;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!speaking_) {
        return AfterSpeech::kNormal;
    }
    speaking_ = false;
    const bool online = !peer_id_.empty() && Now() - peer_seen_us_ < kPeerTimeout;
    if (!online) {
        return AfterSpeech::kListenStudent;
    }
    // Announce the end once the speaker has actually gone quiet (the server's "stop" arrives
    // while the last sentence is still playing).
    end_pending_ = true;
    end_final_ = my_turn_ >= max_turns_;
    end_wait_since_us_ = Now();
    return end_final_ ? AfterSpeech::kListenStudent : AfterSpeech::kHandOff;
}

void DuoTeach::CancelSpeech() {
    std::lock_guard<std::mutex> lock(mutex_);
    end_pending_ = false;
    if (speaking_) {
        speaking_ = false;
        if (enabled_) {
            Broadcast("cancel");
        }
    }
}

void DuoTeach::OnClaimFloor() {
    if (!enabled_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    speaking_ = false;
    end_pending_ = false;
    peer_listening_ = false;
    claim_us_ = Now();
    reply_stage_ = 0;
    Broadcast("claim");
}

void DuoTeach::OnPeerListenStart() {
    std::lock_guard<std::mutex> lock(mutex_);
    peer_listening_ = true;
    peer_listen_since_us_ = Now();
}

void DuoTeach::OnPeerListenEnd() {
    std::lock_guard<std::mutex> lock(mutex_);
    peer_listening_ = false;
}

void DuoTeach::AddSpokenText(const std::string& text) {
    if (!enabled_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!speaking_ || spoken_text_.size() >= kMaxText) {
        return;
    }
    if (!spoken_text_.empty()) {
        spoken_text_ += ' ';
    }
    spoken_text_ = Truncate(spoken_text_ + text, kMaxText);
}

void DuoTeach::OnReplyRequested() {
    std::lock_guard<std::mutex> lock(mutex_);
    reply_stage_ = 3;
    reply_stage_since_us_ = Now();
}

void DuoTeach::OnHeard() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reply_stage_ == 1) {
        reply_stage_ = 2;
        reply_stage_since_us_ = Now();
    }
}

void DuoTeach::HandleMessage(const char* data, int len, const struct sockaddr_in& from_addr) {
    if (len <= static_cast<int>(sizeof(kMagic) - 1) || strncmp(data, kMagic, sizeof(kMagic) - 1) != 0) {
        return;
    }
    cJSON* json = cJSON_ParseWithLength(data + sizeof(kMagic) - 1, len - (sizeof(kMagic) - 1));
    if (json == nullptr) {
        return;
    }
    auto id = cJSON_GetObjectItem(json, "id");
    auto seq = cJSON_GetObjectItem(json, "seq");
    auto boot = cJSON_GetObjectItem(json, "boot");
    auto ev = cJSON_GetObjectItem(json, "ev");
    auto turn = cJSON_GetObjectItem(json, "turn");
    auto final_turn = cJSON_GetObjectItem(json, "final");
    auto spk = cJSON_GetObjectItem(json, "spk");
    auto said = cJSON_GetObjectItem(json, "text");
    if (!cJSON_IsString(id) || !cJSON_IsNumber(seq) || !cJSON_IsString(ev) || my_id_ == id->valuestring) {
        cJSON_Delete(json);
        return;
    }
    const std::string event = ev->valuestring;
    const uint32_t message_seq = static_cast<uint32_t>(seq->valuedouble);
    const uint32_t message_boot = cJSON_IsNumber(boot) ? static_cast<uint32_t>(boot->valuedouble) : 0;
    const int message_turn = cJSON_IsNumber(turn) ? turn->valueint : 0;
    const bool is_final = cJSON_IsTrue(final_turn);
    const bool peer_spk = cJSON_IsTrue(spk);
    const std::string from = id->valuestring;
    std::string message_text = cJSON_IsString(said) ? Truncate(said->valuestring, kMaxText) : "";
    cJSON_Delete(json);
    // The text goes back to the server inside a hand-built JSON string: keep it plain.
    for (auto& c : message_text) {
        if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20) {
            c = ' ';
        }
    }

    auto& app = Application::GetInstance();
    std::unique_lock<std::mutex> lock(mutex_);
    const int64_t now = Now();
    const bool was_online = !peer_id_.empty() && now - peer_seen_us_ < kPeerTimeout;
    if (from != peer_id_ || message_boot != peer_boot_) {
        // New partner, or the partner restarted and counts from 1 again.
        peer_id_ = from;
        peer_boot_ = message_boot;
        peer_seen_seqs_.clear();
    }
    // Every message comes several times (repeats, broadcast and unicast): handle it once.
    if (std::find(peer_seen_seqs_.begin(), peer_seen_seqs_.end(), message_seq) != peer_seen_seqs_.end()) {
        return;
    }
    peer_seen_seqs_.push_back(message_seq);
    if (peer_seen_seqs_.size() > 32) {
        peer_seen_seqs_.erase(peer_seen_seqs_.begin());
    }
    peer_seen_us_ = now;
    peer_ip_ = from_addr.sin_addr.s_addr;
    if (!was_online) {
        ESP_LOGI(TAG, "Partner robot %s online", from.c_str());
        app.Schedule([]() {
            Board::GetInstance().GetDisplay()->ShowNotification("Đã nối với robot bạn", 3000);
        });
    }
    if (peer_spk != peer_speaking_) {
        peer_speaking_ = peer_spk;
        if (!peer_spk) {
            peer_quiet_since_us_ = now;
        }
    }
    if (event != "hi") {
        ESP_LOGI(TAG, "<- %s turn %d%s", event.c_str(), message_turn, is_final ? " (last)" : "");
    }

    if (event == "start") {
        peer_speaking_ = true;
        peer_turn_ = message_turn;
        peer_text_.clear();
        if (speaking_ && my_id_ < peer_id_) {
            // Both started at once: the robot with the smaller id keeps talking, the other yields.
            return;
        }
        lock.unlock();
        app.Schedule([]() { Application::GetInstance().DuoListenToPeer(); });
    } else if (event == "end" || event == "cancel") {
        peer_speaking_ = false;
        peer_quiet_since_us_ = now;
        if (event == "end") {
            peer_turn_ = message_turn;
            peer_text_ = message_text;
        }
        const bool reply = event == "end" && !is_final;
        lock.unlock();
        app.Schedule([reply]() { Application::GetInstance().DuoPeerDone(reply); });
    } else if (event == "claim") {
        peer_speaking_ = false;
        peer_turn_ = 0;
        if (now - claim_us_ < kClaimWindow && my_id_ < peer_id_) {
            return;  // Woken at the same moment; this robot keeps the student.
        }
        lock.unlock();
        app.Schedule([]() { Application::GetInstance().DuoYield(); });
    }
}

void DuoTeach::Task() {
    int sock = -1;
    int64_t last_hello = 0;
    static char buffer[1536];

    while (true) {
        if (!enabled_) {
            if (sock >= 0) {
                close(sock);
                sock = -1;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (sock < 0) {
            // Opening a socket before the network stack is up crashes lwIP ("Invalid mbox"),
            // so wait until the robot has finished starting and connected to Wi-Fi.
            const auto state = Application::GetInstance().GetDeviceState();
            if (state == kDeviceStateUnknown || state == kDeviceStateStarting ||
                state == kDeviceStateWifiConfiguring || state == kDeviceStateActivating ||
                state == kDeviceStateUpgrading) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            int yes = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
            timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in local = {};
            local.sin_family = AF_INET;
            local.sin_port = htons(kPort);
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
                ESP_LOGW(TAG, "Cannot listen on UDP %u", kPort);
                close(sock);
                sock = -1;
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            ESP_LOGI(TAG, "Listening for the partner robot on UDP %u", kPort);
        }

        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        // Empty the socket each time: lwIP keeps only a few datagrams per socket.
        while (len > 0) {
            buffer[len] = '\0';
            HandleMessage(buffer, len, from);
            from_len = sizeof(from);
            len = recvfrom(sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from),
                           &from_len);
        }

        auto& app = Application::GetInstance();
        const int64_t now = Now();
        std::vector<std::string> to_send;
        bool schedule_done = false;
        bool done_reply = false;
        int fallback = 0;  // 1 = ask the server with the partner's text, 2 = listen to the student
        std::string fallback_text;
        uint32_t peer_ip = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (now - last_hello >= kHelloInterval) {
                last_hello = now;
                Broadcast("hi");
            }
            if (end_pending_ && (app.GetAudioService().IsPlaybackIdle() || now - end_wait_since_us_ > 30 * kSecond)) {
                end_pending_ = false;
                Broadcast("end", my_turn_, end_final_, spoken_text_);
            }
            // Safety net if the partner's "end" got lost or the partner disappeared.
            if (peer_listening_) {
                const bool gone = now - peer_seen_us_ > kPeerTimeout;
                const bool quiet = !peer_speaking_ && now - peer_quiet_since_us_ > kQuietGrace &&
                                   now - peer_listen_since_us_ > kQuietGrace;
                const bool too_long = now - peer_listen_since_us_ > kMaxListen;
                if (gone || quiet || too_long) {
                    ESP_LOGW(TAG, "Partner done without an end message (%s)",
                             gone ? "gone" : quiet ? "quiet" : "too long");
                    peer_listening_ = false;
                    schedule_done = true;
                    done_reply = !gone && peer_turn_ < max_turns_;
                }
            }
            // The server never answered the partner: try its words as text, then the student.
            if (reply_stage_ != 0) {
                const int64_t waited = now - reply_stage_since_us_;
                if (reply_stage_ == 1 && waited > kMicReplyWait) {
                    ESP_LOGW(TAG, "No answer to the recording; sending the partner's words as text");
                    fallback = peer_text_.empty() ? 2 : 1;
                    fallback_text = peer_text_;
                } else if (reply_stage_ == 2 && waited > kHeardReplyWait) {
                    ESP_LOGW(TAG, "Server heard the partner but did not answer");
                    fallback = 2;
                } else if (reply_stage_ == 3 && waited > kTextReplyWait) {
                    ESP_LOGW(TAG, "No answer to the partner's words either");
                    fallback = 2;
                }
                if (fallback != 0) {
                    reply_stage_ = fallback == 1 ? 3 : 0;
                    reply_stage_since_us_ = now;
                }
            }
            peer_ip = peer_ip_;
            for (auto& out : outbox_) {
                to_send.push_back(out.payload);
                --out.copies_left;
            }
            outbox_.erase(std::remove_if(outbox_.begin(), outbox_.end(),
                                         [](const Outgoing& o) { return o.copies_left <= 0; }),
                          outbox_.end());
        }
        if (schedule_done) {
            app.Schedule([done_reply]() { Application::GetInstance().DuoPeerDone(done_reply); });
        }
        if (fallback == 1) {
            app.Schedule([text = std::move(fallback_text)]() { Application::GetInstance().DuoAskWithText(text); });
        } else if (fallback == 2) {
            app.Schedule([]() { Application::GetInstance().DuoListenToStudent(); });
        }

        if (!to_send.empty()) {
            sockaddr_in to = {};
            to.sin_family = AF_INET;
            to.sin_port = htons(kPort);
            to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sockaddr_in peer = to;
            peer.sin_addr.s_addr = peer_ip;
            for (const auto& payload : to_send) {
                sendto(sock, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
                // Broadcasts are often lost while Wi-Fi sleeps; unicast is acknowledged and retried.
                if (peer_ip != 0) {
                    sendto(sock, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&peer),
                           sizeof(peer));
                }
            }
        }
    }
}
