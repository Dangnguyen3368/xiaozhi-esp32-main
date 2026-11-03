#include "bank_notify.h"

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "photo_solver.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <wifi_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <deque>
#include <mutex>
#include <string_view>

// Voice clips from tools/money_voice/gen.py, embedded by main/CMakeLists.txt.
#define MONEY_CLIP(name)                                                     \
    extern const char money_##name##_start[] asm("_binary_" #name "_ogg_start"); \
    extern const char money_##name##_end[] asm("_binary_" #name "_ogg_end");
MONEY_CLIP(da_nhan)
MONEY_CLIP(dong)
MONEY_CLIP(khong)
MONEY_CLIP(mot)
MONEY_CLIP(hai)
MONEY_CLIP(ba)
MONEY_CLIP(bon)
MONEY_CLIP(nam)
MONEY_CLIP(sau)
MONEY_CLIP(bay)
MONEY_CLIP(tam)
MONEY_CLIP(chin)
MONEY_CLIP(muoi)
MONEY_CLIP(muoi2)
MONEY_CLIP(mot2)
MONEY_CLIP(lam)
MONEY_CLIP(linh)
MONEY_CLIP(tram)
MONEY_CLIP(nghin)
MONEY_CLIP(trieu)
MONEY_CLIP(ty)
#undef MONEY_CLIP

namespace bank_notify {
namespace {

constexpr char kTag[] = "BankNotify";
constexpr size_t kMaxBody = 4096;
constexpr int64_t kDuplicateUs = 60LL * 1000 * 1000;  // banks and MacroDroid can fire twice
constexpr size_t kRecentCount = 10;

struct Entry {
    int64_t time_us;
    uint64_t amount;
    std::string text;
};

std::mutex mutex_;
std::string key_;
std::deque<Entry> recent_;
std::function<void(const std::string&, const std::string&)> viewer_;
std::function<void()> setup_viewer_;
std::mutex speak_mutex_;  // one announcement at a time, in order

#define CLIP(name) {#name, std::string_view(money_##name##_start, money_##name##_end - money_##name##_start)}
const std::pair<const char*, std::string_view> kClips[] = {
    CLIP(da_nhan), CLIP(dong), CLIP(khong), CLIP(mot),   CLIP(hai),  CLIP(ba),   CLIP(bon),
    CLIP(nam),     CLIP(sau),  CLIP(bay),   CLIP(tam),   CLIP(chin), CLIP(muoi), CLIP(muoi2),
    CLIP(mot2),    CLIP(lam),  CLIP(linh),  CLIP(tram),  CLIP(nghin), CLIP(trieu), CLIP(ty),
};
#undef CLIP

std::string_view Clip(const char* name) {
    for (const auto& clip : kClips) {
        if (std::string_view(clip.first) == name) {
            return clip.second;
        }
    }
    return {};
}

const std::string& Key() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_.empty()) {
        Settings settings("bank", true);
        key_ = settings.GetString("key");
        if (key_.empty()) {
            char buffer[9];
            snprintf(buffer, sizeof(buffer), "%08lx", static_cast<unsigned long>(esp_random()));
            key_ = buffer;
            settings.SetString("key", key_);
        }
    }
    return key_;
}

// "1500000" -> "1.500.000"
std::string FormatAmount(uint64_t amount) {
    std::string digits = std::to_string(amount);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += '.';
        }
        out += digits[i];
    }
    return out;
}

std::string BaseUrl() {
    const std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty() || !photo_solver::StartServer()) {
        return std::string();
    }
    return "http://" + ip + ":" + std::to_string(photo_solver::kWebPort);
}

// MacroDroid may send JSON; join its text fields. Anything else is used as is.
std::string BodyText(const std::string& body) {
    cJSON* json = body.empty() || body[0] != '{' ? nullptr : cJSON_Parse(body.c_str());
    if (json == nullptr) {
        return body;
    }
    std::string text;
    static const char* const kFields[] = {"title", "text", "message", "content", "body", "notification"};
    for (const char* field : kFields) {
        cJSON* item = cJSON_GetObjectItem(json, field);
        if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
            if (!text.empty()) {
                text += '\n';
            }
            text += item->valuestring;
        }
    }
    cJSON_Delete(json);
    return text.empty() ? body : text;
}

void Speak(uint64_t amount) {
    auto& app = Application::GetInstance();
    // Wait for the assistant to finish talking; the radio writes to the speaker directly, so
    // while it plays the notification is only shown.
    for (int i = 0; i < 40 && app.GetDeviceState() != kDeviceStateIdle; ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (app.GetDeviceState() != kDeviceStateIdle || app.IsRadioPlaybackActive()) {
        ESP_LOGI(kTag, "Busy, not reading the amount aloud");
        return;
    }
    app.PlaySound(Lang::Sounds::OGG_SUCCESS);
    app.PlaySound(Clip("da_nhan"));
    for (const char* word : AmountWords(amount)) {
        app.PlaySound(Clip(word));
    }
    app.PlaySound(Clip("dong"));
}

void Handle(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\r')) {
        text.pop_back();
    }
    if (text.empty()) {
        return;
    }
    const uint64_t amount = ParseIncomingAmount(text);
    const int64_t now = esp_timer_get_time();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : recent_) {
            if (entry.text == text && now - entry.time_us < kDuplicateUs) {
                ESP_LOGI(kTag, "Duplicate notification ignored");
                return;
            }
        }
        recent_.push_front(Entry{now, amount, text});
        if (recent_.size() > kRecentCount) {
            recent_.pop_back();
        }
    }
    ESP_LOGI(kTag, "Notification (+%llu): %s", static_cast<unsigned long long>(amount), text.c_str());
    const std::string title = amount > 0 ? "+" + FormatAmount(amount) + " đ" : "Thông báo ngân hàng";
    if (viewer_) {
        viewer_(title, text);
    } else if (auto* display = Board::GetInstance().GetDisplay()) {
        display->ShowNotification(title, 8000);
        display->SetChatMessage("system", text.c_str());
    }
    std::lock_guard<std::mutex> lock(speak_mutex_);
    if (amount > 0) {
        Speak(amount);
    }
}

void AnnounceTask(void* arg) {
    std::unique_ptr<std::string> text(static_cast<std::string*>(arg));
    Handle(std::move(*text));
    vTaskDelete(nullptr);
}

bool KeyMatches(httpd_req_t* req) {
    char query[96];
    char key[32];
    return httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
           httpd_query_key_value(query, "key", key, sizeof(key)) == ESP_OK && Key() == key;
}

esp_err_t HandlePost(httpd_req_t* req) {
    if (!KeyMatches(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Sai key.");
        return ESP_FAIL;
    }
    const size_t length = req->content_len;
    if (length == 0 || length > kMaxBody) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Tin rỗng hoặc quá dài.");
        return ESP_FAIL;
    }
    std::string body(length, '\0');
    size_t total = 0;
    while (total < length) {
        const int read = httpd_req_recv(req, body.data() + total, length - total);
        if (read == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (read <= 0) {
            return ESP_FAIL;
        }
        total += read;
    }
    Announce(BodyText(body));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

constexpr char kSetupPage[] = R"HTML(<!doctype html><html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Báo chuyển khoản</title>
<style>body{font-family:sans-serif;margin:16px;max-width:560px;line-height:1.45}
code{display:block;word-break:break-all;background:#eef;padding:10px;border-radius:8px;font-size:15px}
button{font-size:17px;padding:10px 16px;margin:8px 8px 0 0;border-radius:8px;border:0;background:#1565c0;color:#fff}
li{margin:6px 0}#st{color:#2e7d32;font-weight:bold}</style></head><body>
<h2>Loa báo chuyển khoản</h2>
<p>Địa chỉ gửi thông báo cho robot:</p><code id="u">@URL@</code>
<button onclick="navigator.clipboard.writeText(document.getElementById('u').textContent).then(()=>st.textContent='Đã chép địa chỉ.')">Chép địa chỉ</button>
<button onclick="fetch(document.getElementById('u').textContent,{method:'POST',body:'Thử: TK 0123 +500,000 VND. Số dư 1,500,000 VND'}).then(r=>st.textContent=r.ok?'Robot đã nhận, nghe thử nhé.':'Lỗi '+r.status)">Thử 500.000đ</button>
<p id="st"></p>
<h3>Cài trên Android (MacroDroid, miễn phí)</h3><ol>
<li>Cài <b>MacroDroid</b> từ CH Play, mở app, bấm <b>Thêm Macro</b>.</li>
<li><b>Trình kích hoạt</b>: Thiết bị/Ứng dụng → <b>Thông báo</b> → Đã nhận thông báo → chọn app ngân hàng (và ví như MoMo), nội dung: Bất kỳ. Cho MacroDroid quyền đọc thông báo khi được hỏi.</li>
<li><b>Hành động</b>: Kết nối/Web → <b>Yêu cầu HTTP</b>: phương thức <b>POST</b>, URL dán địa chỉ ở trên, nội dung (Body) kiểu text/plain: bấm nút chèn biến <b>[...]</b>, chọn <b>Tiêu đề thông báo</b> rồi <b>Nội dung thông báo</b>.</li>
<li>Lưu macro. Điện thoại và robot phải cùng Wi-Fi nhà.</li></ol>
<p>Nên đặt IP cố định cho robot trong modem (DHCP reservation) để địa chỉ không đổi.</p>
<p><b>iPhone</b> không đọc được thông báo của app khác, chỉ dùng được <b>SMS</b> (hoặc email) của ngân hàng: Phím tắt → Tự động hóa → Tin nhắn (người gửi là ngân hàng, hoặc chứa "VND") → Chạy ngay lập tức → tác vụ <b>Lấy nội dung của URL</b>: dán địa chỉ trên, Phương thức POST, Phần thân JSON, khóa <b>text</b> = Đầu vào phím tắt.</p>
</body></html>)HTML";

esp_err_t HandleSetup(httpd_req_t* req) {
    if (!KeyMatches(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Sai key: quét lại mã QR trên robot.");
        return ESP_FAIL;
    }
    std::string page = kSetupPage;
    const std::string url = BaseUrl() + "/bank?key=" + Key();
    page.replace(page.find("@URL@"), 5, url);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page.data(), page.size());
}

// Starts the web page once Wi-Fi has an address, so the phone can reach it after a reboot.
void StartTask(void*) {
    while (WifiManager::GetInstance().GetIpAddress().empty()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    photo_solver::StartServer();
    vTaskDelete(nullptr);
}

std::string Ago(int64_t time_us) {
    const int64_t minutes = (esp_timer_get_time() - time_us) / 60000000LL;
    if (minutes < 1) {
        return "vừa xong";
    }
    if (minutes < 60) {
        return std::to_string(minutes) + " phút trước";
    }
    return std::to_string(minutes / 60) + " giờ trước";
}

}  // namespace

void SetViewer(std::function<void(const std::string& title, const std::string& text)> viewer) {
    viewer_ = std::move(viewer);
}

void SetSetupViewer(std::function<void()> viewer) { setup_viewer_ = std::move(viewer); }

std::string SetupUrl() {
    const std::string base = BaseUrl();
    return base.empty() ? base : base + "/nganhang?key=" + Key();
}

void Announce(const std::string& text) {
    auto* copy = new std::string(text);
    if (xTaskCreate(AnnounceTask, "bank_say", 6144, copy, 3, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Cannot start bank_say task");
        delete copy;
    }
}

void Start() {
    Key();
    photo_solver::AddRoute("/bank", HTTP_POST, HandlePost);
    photo_solver::AddRoute("/nganhang", HTTP_GET, HandleSetup);
    xTaskCreate(StartTask, "bank_start", 3072, nullptr, 2, nullptr);

    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.bank.recent",
        "Bank transfer notifications the robot received from the user's phone (loa báo chuyển "
        "khoản), newest first, with the amount received. Use for: vừa nhận bao nhiêu tiền, ai "
        "chuyển khoản, đọc lại giao dịch.",
        PropertyList(), [](const PropertyList&) -> ToolResult {
            std::lock_guard<std::mutex> lock(mutex_);
            if (recent_.empty()) {
                return std::string("Chưa nhận thông báo chuyển khoản nào từ lúc robot bật.");
            }
            std::string out;
            for (const auto& entry : recent_) {
                out += "- " + Ago(entry.time_us) + ": " +
                       (entry.amount > 0 ? "nhận +" + FormatAmount(entry.amount) + " đồng. " : "") +
                       entry.text + "\n";
            }
            return out;
        });
    mcp.AddTool(
        "self.bank.setup",
        "Show on the robot's screen the QR code to set up bank transfer announcements (cài loa "
        "báo chuyển khoản / thông báo ngân hàng). The user scans it with the phone on the same "
        "Wi-Fi to get the address and the MacroDroid steps.",
        PropertyList(), [](const PropertyList&) -> ToolResult {
            const std::string url = SetupUrl();
            if (url.empty()) {
                return std::string("ERROR: robot chưa có Wi-Fi.");
            }
            if (setup_viewer_) {
                setup_viewer_();
            } else if (auto* display = Board::GetInstance().GetDisplay()) {
                display->ShowNotification("Mở: " + url, 30000);
            }
            return "Đã hiện mã QR (" + url +
                   "). Bảo người dùng quét bằng điện thoại cùng Wi-Fi rồi làm theo hướng dẫn.";
        });
}

}  // namespace bank_notify
