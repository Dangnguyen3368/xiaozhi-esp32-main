#include "pc_control.h"

#include "local_pc.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>

namespace {
constexpr char kTag[] = "PcControl";
constexpr size_t kMaxTextLength = 2000;
constexpr size_t kMaxReplyLength = 1024;

struct JsonDeleter {
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string ToJson(cJSON* json) {
    char* printed = cJSON_PrintUnformatted(json);
    std::string text = printed != nullptr ? printed : "{}";
    cJSON_free(printed);
    return text;
}

// The computer replies {"ok": true, "message": "..."} or {"error": "..."}.
std::string Describe(const std::string& reply) {
    JsonPtr json(cJSON_Parse(reply.c_str()));
    if (!json) {
        return "ERROR: máy tính trả lời lạ (chạy bản robot-may-tinh.bat mới chưa?).";
    }
    cJSON* error = cJSON_GetObjectItem(json.get(), "error");
    if (cJSON_IsString(error)) {
        return std::string("ERROR: ") + error->valuestring;
    }
    cJSON* message = cJSON_GetObjectItem(json.get(), "message");
    return cJSON_IsString(message) ? message->valuestring : "Done.";
}
}  // namespace

PcControl& PcControl::GetInstance() {
    static PcControl instance;
    return instance;
}

std::string PcControl::PostOnce(const std::string& base_url, const std::string& path, const std::string& body,
                                bool& connected) {
    connected = false;
    const std::string url = base_url + path;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return "ERROR: out of memory.";
    }
    Settings settings("pc");
    const std::string token = settings.GetString("token");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!token.empty()) {
        esp_http_client_set_header(client, "X-Robot-Token", token.c_str());
    }
    std::string reply;
    if (esp_http_client_open(client, static_cast<int>(body.size())) == ESP_OK) {
        connected = true;
        esp_http_client_write(client, body.data(), static_cast<int>(body.size()));
        esp_http_client_fetch_headers(client);
        char buffer[256];
        int read = 0;
        while ((read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0 && reply.size() < kMaxReplyLength) {
            reply.append(buffer, read);
        }
        if (reply.empty()) {
            reply = "{\"error\":\"empty reply (HTTP " + std::to_string(esp_http_client_get_status_code(client)) + ")\"}";
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return reply;
}

std::string PcControl::Post(const std::string& path, const std::string& body) {
    if (base_url_.empty()) {
        base_url_ = local_pc::Cached();
    }
    bool connected = false;
    std::string reply;
    if (!base_url_.empty()) {
        reply = PostOnce(base_url_, path, body, connected);
    }
    if (!connected) {
        // The computer may have a new address; listen for its announcement once.
        base_url_ = local_pc::Find(4);
        if (base_url_.empty()) {
            return "ERROR: không thấy máy tính. Bật robot-may-tinh.bat trên máy tính cùng mạng Wi-Fi.";
        }
        reply = PostOnce(base_url_, path, body, connected);
        if (!connected) {
            return "ERROR: không kết nối được máy tính " + base_url_ +
                   ". Kiểm tra robot-may-tinh.bat còn chạy và tường lửa đã cho phép.";
        }
    }
    return reply;
}

bool PcControl::IsPaired() const {
    Settings settings("pc");
    return !settings.GetString("token").empty();
}

std::string PcControl::Run(const std::string& action, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (action == "pair") {
        JsonPtr body(cJSON_CreateObject());
        cJSON_AddStringToObject(body.get(), "code", text.c_str());
        const std::string reply = Post("/pair", ToJson(body.get()));
        if (reply.starts_with("ERROR:")) {
            return reply;
        }
        JsonPtr json(cJSON_Parse(reply.c_str()));
        cJSON* token = json ? cJSON_GetObjectItem(json.get(), "token") : nullptr;
        if (!cJSON_IsString(token)) {
            return Describe(reply);
        }
        Settings settings("pc", true);
        settings.SetString("token", token->valuestring);
        ESP_LOGI(kTag, "Paired with the computer");
        return "Đã ghép với máy tính.";
    }
    if (action != "type" && action != "key" && action != "open") {
        return "ERROR: use action pair, type, key or open.";
    }
    if (text.empty()) {
        return "ERROR: `text` is empty.";
    }
    if (text.size() > kMaxTextLength) {
        return "ERROR: text too long; send it in shorter parts.";
    }
    JsonPtr body(cJSON_CreateObject());
    cJSON_AddStringToObject(body.get(), "action", action.c_str());
    cJSON_AddStringToObject(body.get(), "text", text.c_str());
    const std::string reply = Post("/control", ToJson(body.get()));
    if (reply.starts_with("ERROR:")) {
        return reply;
    }
    return Describe(reply);
}

namespace {
struct AsyncJob {
    std::string action;
    std::string text;
};

void AsyncTask(void* arg) {
    std::unique_ptr<AsyncJob> job(static_cast<AsyncJob*>(arg));
    const std::string result = PcControl::GetInstance().Run(job->action, job->text);
    if (auto* display = Board::GetInstance().GetDisplay()) {
        if (result.starts_with("ERROR:")) {
            display->ShowNotification("Lỗi:" + result.substr(6), 6000);
        } else if (job->action == "pair") {
            display->ShowNotification(result, 4000);
        } else {
            display->ShowNotification("Đã gửi tới máy tính", 2000);
        }
    }
    vTaskDelete(nullptr);
}
}  // namespace

void PcControl::RunAsync(const std::string& action, const std::string& text) {
    auto* job = new AsyncJob{action, text};
    if (xTaskCreate(AsyncTask, "pc_control", 6144, job, 3, nullptr) != pdPASS) {
        delete job;
        ESP_LOGE(kTag, "Cannot start pc_control task");
    }
}

void PcControl::RegisterTool() {
    McpServer::GetInstance().AddTool(
        "self.pc.control",
        "Control the user's Windows computer on the same Wi-Fi (it must run robot-may-tinh.bat). "
        "Actions: `pair` with `code` = the 4-digit code shown in that program's window (do this "
        "once when the computer says the robot is not paired); `type` writes `text` at the cursor, "
        "for dictation into Word, Excel, Notepad and so on (write exactly what the user dictates, "
        "with Vietnamese diacritics and normal punctuation; do not add your own words); `key` "
        "presses keys given in `text`, e.g. enter, tab, backspace, delete, up, down, left, right, "
        "home, end, esc, ctrl+s, ctrl+z, ctrl+a, ctrl+c, ctrl+v, ctrl+b, ctrl+n, alt+tab, alt+f4, "
        "win (alt+f4 closes the active window, e.g. when the user says close Word); several keys separated by spaces are pressed in turn (\"tab tab enter\"). In Excel, "
        "type a cell's value then press tab (next column) or enter (next row). `open` starts an "
        "app named in `text`: word, excel, powerpoint, notepad, calculator, browser. Never type "
        "passwords or anything the user did not ask for.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("text", kPropertyTypeString, std::string("")),
                      Property("code", kPropertyTypeString, std::string(""))}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto action = properties["action"].value<std::string>();
            const auto text = action == "pair" ? properties["code"].value<std::string>()
                                               : properties["text"].value<std::string>();
            return Run(action, text);
        });
}
