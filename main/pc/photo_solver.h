#pragma once

#include <esp_http_server.h>

#include <functional>
#include <string>

// Explains a photo the user takes with their phone, without a computer: the robot serves a small
// web page (http://<robot>:8080/chup) on the home Wi-Fi; the phone takes the picture and uploads
// it to the robot, which sends it to the image explain service the xiaozhi server announces at
// session start.
namespace photo_solver {

// Adds the self.pc.explain_photo MCP tool.
void RegisterTool();

// Called (from a background task) with the solution of each photo the phone uploads, so the
// board can show it on screen right away.
void SetSolutionViewer(std::function<void(const std::string& text)> viewer);

// Shows the QR page for the phone (called on the main task when the voice tool needs a photo).
void SetQrViewer(std::function<void()> viewer);

// Starts the robot's photo page if needed and returns its address for the phone
// ("http://<robot ip>:8080/chup"); empty when the robot has no Wi-Fi address.
std::string PhoneUrl();

// The robot's web server on port 8080 (shared with other features, like bank notifications).
// StartServer() is safe to call again; AddRoute() works before or after it starts.
constexpr int kWebPort = 8080;
bool StartServer();
void AddRoute(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t* req));

}  // namespace photo_solver
