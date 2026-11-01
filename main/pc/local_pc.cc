#include "local_pc.h"

#include "settings.h"

#include <esp_log.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <cstdio>

namespace local_pc {
namespace {
constexpr char kTag[] = "LocalPc";
constexpr uint16_t kDiscoveryPort = 8766;
}  // namespace

std::string Cached() {
    Settings settings("radio");
    return settings.GetString("local_url");
}

std::string Find(int seconds, const std::atomic<bool>* cancel) {
    std::string found;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        timeval timeout = {.tv_sec = 1, .tv_usec = 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(kDiscoveryPort);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0) {
            char buffer[64];
            for (int second = 0; second < seconds && !(cancel != nullptr && cancel->load()); ++second) {
                sockaddr_in from = {};
                socklen_t from_len = sizeof(from);
                const int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                         reinterpret_cast<sockaddr*>(&from), &from_len);
                if (len <= 0) {
                    continue;
                }
                buffer[len] = '\0';
                int port = 0;
                if (sscanf(buffer, "XIAOZHI_MUSIC %d", &port) != 1 || port <= 0 || port > 65535) {
                    continue;
                }
                char ip[16];
                inet_ntoa_r(from.sin_addr, ip, sizeof(ip));
                found = std::string("http://") + ip + ":" + std::to_string(port);
                ESP_LOGI(kTag, "Computer found at %s", found.c_str());
                Settings settings("radio", true);
                settings.SetString("local_url", found);
                break;
            }
        } else {
            ESP_LOGW(kTag, "Cannot listen on UDP %u", kDiscoveryPort);
        }
        close(sock);
    }
    if (found.empty()) {
        found = Cached();
        if (!found.empty()) {
            ESP_LOGW(kTag, "No announcement; using last address %s", found.c_str());
        }
    }
    return found;
}

}  // namespace local_pc
