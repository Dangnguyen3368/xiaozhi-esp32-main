#include "photo_solver.h"

#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "system_info.h"

#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <wifi_manager.h>

#include <memory>
#include <mutex>

namespace photo_solver {
namespace {

constexpr char kTag[] = "PhotoSolver";
constexpr int kPort = 8080;
constexpr size_t kMaxPhotoBytes = 1536 * 1024;
constexpr int64_t kMaxPhotoAgeUs = 30LL * 60 * 1000 * 1000;
constexpr char kDefaultQuestion[] =
    "Đây là ảnh chụp một đề bài. Hãy chép lại đề, rồi giải chi tiết từng bước bằng tiếng Việt, "
    "ghi rõ công thức và đáp số.";

constexpr char kPhonePage[] = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Chụp đề cho robot</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#f4f6fb;color:#1c2333;text-align:center}
h1{font-size:22px;margin:8px 0 4px}
p{margin:6px 0;color:#4a5468}
label{display:block;margin:18px auto;max-width:360px;padding:22px;border-radius:16px;background:#2f6fed;
color:#fff;font-size:22px;font-weight:600}
input{display:none}
img{max-width:100%;max-height:55vh;border-radius:12px;margin-top:12px}
#st{font-size:18px;font-weight:600;margin-top:12px}
</style></head><body>
<h1>Chụp đề cho robot</h1>
<p>Chụp rõ, đủ sáng, đề nằm ngang màn hình.</p>
<label>&#128247; Chụp đề<input id="f" type="file" accept="image/*" capture="environment"></label>
<div id="st"></div><img id="pv" alt="">
<script>
const st=document.getElementById('st'),pv=document.getElementById('pv');
document.getElementById('f').onchange=e=>{
  const file=e.target.files[0]; if(!file) return;
  st.textContent='Đang thu nhỏ ảnh...';
  const img=new Image();
  img.onload=()=>{
    const k=Math.min(1,1280/Math.max(img.width,img.height));
    const c=document.createElement('canvas');
    c.width=Math.round(img.width*k); c.height=Math.round(img.height*k);
    c.getContext('2d').drawImage(img,0,0,c.width,c.height);
    pv.src=c.toDataURL('image/jpeg',0.8);
    c.toBlob(b=>{
      st.textContent='Đang gửi...';
      fetch('/photo',{method:'POST',headers:{'Content-Type':'image/jpeg'},body:b})
        .then(r=>r.ok?r.text():Promise.reject(r.status))
        .then(()=>{st.textContent='Đã gửi! Giờ nói với robot: "giải bài vừa chụp".'})
        .catch(x=>{st.textContent='Gửi lỗi ('+x+'). Thử lại.'});
    },'image/jpeg',0.85);
    URL.revokeObjectURL(img.src);
  };
  img.onerror=()=>{st.textContent='Không đọc được ảnh này.'};
  img.src=URL.createObjectURL(file);
};
</script></body></html>)HTML";

struct Photo {
    std::shared_ptr<char> data;
    size_t size = 0;
    int64_t taken_us = 0;
};

std::mutex mutex_;
httpd_handle_t server_ = nullptr;
Photo photo_;

void Notify(const std::string& text, int ms) {
    if (auto* display = Board::GetInstance().GetDisplay()) {
        display->ShowNotification(text, ms);
    }
}

esp_err_t HandlePage(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kPhonePage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HandleUpload(httpd_req_t* req) {
    const size_t length = req->content_len;
    if (length < 4 || length > kMaxPhotoBytes) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ảnh rỗng hoặc quá lớn.");
        return ESP_FAIL;
    }
    std::shared_ptr<char> data(
        static_cast<char*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        heap_caps_free);
    if (!data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Robot không đủ bộ nhớ.");
        return ESP_FAIL;
    }
    size_t total = 0;
    while (total < length) {
        const int read = httpd_req_recv(req, data.get() + total, length - total);
        if (read == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (read <= 0) {
            return ESP_FAIL;
        }
        total += read;
    }
    if (static_cast<unsigned char>(data.get()[0]) != 0xFF ||
        static_cast<unsigned char>(data.get()[1]) != 0xD8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Không phải ảnh JPEG.");
        return ESP_FAIL;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        photo_ = Photo{std::move(data), total, esp_timer_get_time()};
    }
    ESP_LOGI(kTag, "Received %u-byte photo from the phone", static_cast<unsigned>(total));
    Notify("Đã nhận ảnh. Nói \"giải bài vừa chụp\"", 8000);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

bool StartServer() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_ != nullptr) {
        return true;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kPort;
    config.ctrl_port = 32770;  // the Wi-Fi setup page uses the default one
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 15;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot start the photo page on port %d", kPort);
        server_ = nullptr;
        return false;
    }
    httpd_uri_t page = {};
    page.uri = "/chup";
    page.method = HTTP_GET;
    page.handler = HandlePage;
    httpd_register_uri_handler(server_, &page);
    httpd_uri_t root = page;
    root.uri = "/";
    httpd_register_uri_handler(server_, &root);
    httpd_uri_t upload = {};
    upload.uri = "/photo";
    upload.method = HTTP_POST;
    upload.handler = HandleUpload;
    httpd_register_uri_handler(server_, &upload);
    ESP_LOGI(kTag, "Photo page started on port %d", kPort);
    return true;
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
    const std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty() || !StartServer()) {
        return std::string();
    }
    return "http://" + ip + ":" + std::to_string(kPort) + "/chup";
}

void RegisterTool() {
    McpServer::GetInstance().AddTool(
        "self.pc.explain_photo",
        "Look at the photo the user just took with their phone and answer `question` about it. "
        "The phone sends photos straight to the robot: on the same Wi-Fi it opens the address this "
        "tool reports (http://<robot>:8080/chup, also shown by the robot's Menu > Máy tính > Chụp "
        "đề button) and taps Chụp đề. Use this when the user says giải bài vừa chụp, xem ảnh vừa "
        "chụp, đọc đề trong ảnh, or asks how to send a photo. Leave `question` empty to read and "
        "solve the problem in the photo. Returns the vision AI's answer; explain it to the user in "
        "Vietnamese, step by step. If it says there is no new photo, tell the user the address.",
        PropertyList({Property("question", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ToolResult {
            std::string question = properties["question"].value<std::string>();
            if (question.empty()) {
                question = kDefaultQuestion;
            }
            const std::string url = PhoneUrl();
            Photo photo;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                photo = photo_;
            }
            if (!photo.data || esp_timer_get_time() - photo.taken_us > kMaxPhotoAgeUs) {
                const std::string error =
                    url.empty() ? std::string("ERROR: robot chưa có Wi-Fi nên chưa nhận ảnh được.")
                                : "ERROR: chưa có ảnh mới. Trên điện thoại cùng Wi-Fi mở " + url +
                                      ", bấm Chụp đề rồi gửi.";
                Notify(error.substr(7), 15000);
                return error;
            }
            if (McpServer::GetInstance().vision_url().empty()) {
                return std::string(
                    "ERROR: server xiaozhi chưa cấp dịch vụ xem ảnh cho robot này, nên chưa xem "
                    "được ảnh.");
            }
            Notify("Đang xem ảnh vừa chụp...", 4000);
            return Explain(photo.data.get(), photo.size, question);
        });
}

}  // namespace photo_solver
