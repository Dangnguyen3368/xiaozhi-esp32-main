#include "photo_solver.h"

#include "local_pc.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "system_info.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <memory>

namespace photo_solver {
namespace {

constexpr char kTag[] = "PhotoSolver";
constexpr size_t kMaxPhotoBytes = 1536 * 1024;
constexpr char kDefaultQuestion[] =
    "Đây là ảnh chụp một đề bài. Hãy chép lại đề, rồi giải chi tiết từng bước bằng tiếng Việt, "
    "ghi rõ công thức và đáp số.";

using Buffer = std::unique_ptr<char, decltype(&heap_caps_free)>;

void Notify(const std::string& text, int ms) {
    if (auto* display = Board::GetInstance().GetDisplay()) {
        display->ShowNotification(text, ms);
    }
}

std::string PcBase() {
    std::string base = local_pc::Cached();
    if (base.empty()) {
        base = local_pc::Find(4);
    }
    return base;
}

// Downloads the latest phone photo from the computer. On success `size` is set and the buffer
// returned; otherwise `error` explains why.
Buffer DownloadPhoto(const std::string& base, size_t& size, std::string& error) {
    Buffer none(nullptr, heap_caps_free);
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    Settings settings("pc");
    const std::string token = settings.GetString("token");
    if (!token.empty()) {
        http->SetHeader("X-Robot-Token", token);
    }
    if (auto opened = http->Open("GET", base + "/photo"); !opened) {
        error = "ERROR: không kết nối được máy tính " + base + ". Bật robot-may-tinh.bat chưa?";
        return none;
    }
    auto status = http->GetStatusCode();
    if (!status) {
        http->Close();
        error = "ERROR: máy tính không trả lời.";
        return none;
    }
    if (*status == 404) {
        http->Close();
        error = "ERROR: chưa có ảnh mới. Trên điện thoại cùng Wi-Fi mở " + base +
                "/chup, bấm Chụp đề rồi gửi.";
        return none;
    }
    if (*status == 403) {
        http->Close();
        error = "ERROR: robot chưa ghép với máy tính (dùng action pair của self.pc.control).";
        return none;
    }
    if (*status != 200) {
        http->Close();
        error = "ERROR: máy tính trả lời HTTP " + std::to_string(*status) +
                " (chạy bản robot-may-tinh.bat mới chưa?).";
        return none;
    }
    const size_t length = http->GetBodyLength();
    if (length == 0 || length > kMaxPhotoBytes) {
        http->Close();
        error = "ERROR: ảnh rỗng hoặc quá lớn.";
        return none;
    }
    Buffer data(static_cast<char*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                heap_caps_free);
    if (!data) {
        http->Close();
        error = "ERROR: không đủ bộ nhớ cho ảnh.";
        return none;
    }
    size_t total = 0;
    while (total < length) {
        auto read = http->Read(data.get() + total, length - total);
        if (!read || *read <= 0) {
            break;
        }
        total += *read;
    }
    http->Close();
    if (total != length) {
        error = "ERROR: tải ảnh từ máy tính bị đứt.";
        return none;
    }
    size = total;
    return data;
}

// Sends the JPEG to the server's image explain service, the same request a camera board makes.
std::string Explain(const char* jpeg, size_t size, const std::string& question) {
    auto& mcp = McpServer::GetInstance();
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    const std::string boundary = "----XIAOZHI_PHONE_PHOTO_BOUNDARY";
    http->SetTimeout(30000);
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    if (!mcp.vision_token().empty()) {
        http->SetHeader("Authorization", "Bearer " + mcp.vision_token());
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (auto opened = http->Open("POST", mcp.vision_url()); !opened) {
        return "ERROR: không kết nối được dịch vụ xem ảnh: " + opened.error().ToString();
    }
    std::string head;
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"question\"\r\n\r\n";
    head += question + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"phone.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";

    bool ok = static_cast<bool>(http->Write(head.data(), head.size()));
    constexpr size_t kChunk = 8192;
    for (size_t sent = 0; ok && sent < size; sent += kChunk) {
        const size_t n = size - sent < kChunk ? size - sent : kChunk;
        ok = static_cast<bool>(http->Write(jpeg + sent, n));
    }
    ok = ok && http->Write(tail.data(), tail.size()) && http->Write("", 0);
    if (!ok) {
        http->Close();
        return "ERROR: gửi ảnh lên dịch vụ xem ảnh bị lỗi.";
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        http->Close();
        return "ERROR: dịch vụ xem ảnh trả lời lỗi" +
               (status ? " HTTP " + std::to_string(*status) : std::string()) + ".";
    }
    std::string result = http->ReadAll();
    http->Close();
    ESP_LOGI(kTag, "Explained %u-byte photo: %s", static_cast<unsigned>(size), result.c_str());
    return result;
}

}  // namespace

std::string PhoneUrl() {
    const std::string base = local_pc::Cached();
    return base.empty() ? std::string() : base + "/chup";
}

void RegisterTool() {
    McpServer::GetInstance().AddTool(
        "self.pc.explain_photo",
        "Look at the photo the user just took with their phone and answer `question` about it. "
        "The phone sends photos through the computer program robot-may-tinh.bat: on the same "
        "Wi-Fi the user opens http://<computer>:8765/chup in the phone browser and taps Chụp đề. "
        "Use this when the user says giải bài vừa chụp, xem ảnh vừa chụp, đọc đề trong ảnh. Leave "
        "`question` empty to read and solve the problem in the photo. Returns the vision AI's "
        "answer; explain it to the user in Vietnamese, step by step. If it says there is no new "
        "photo, tell the user the address to open on the phone.",
        PropertyList({Property("question", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ToolResult {
            std::string question = properties["question"].value<std::string>();
            if (question.empty()) {
                question = kDefaultQuestion;
            }
            if (McpServer::GetInstance().vision_url().empty()) {
                return std::string(
                    "ERROR: server xiaozhi chưa cấp dịch vụ xem ảnh cho robot này, nên chưa xem "
                    "được ảnh.");
            }
            const std::string base = PcBase();
            if (base.empty()) {
                return std::string(
                    "ERROR: không thấy máy tính. Bật robot-may-tinh.bat trên máy tính cùng Wi-Fi.");
            }
            Notify("Đang xem ảnh vừa chụp...", 4000);
            size_t size = 0;
            std::string error;
            Buffer photo = DownloadPhoto(base, size, error);
            if (!photo) {
                Notify(error.substr(7), 8000);
                return error;
            }
            return Explain(photo.get(), size, question);
        });
}

}  // namespace photo_solver
