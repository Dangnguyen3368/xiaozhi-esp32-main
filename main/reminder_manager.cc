#include "reminder_manager.h"

#include <esp_log.h>
#include <cJSON.h>
#include <nvs.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#define TAG "Reminder"
#define REMINDER_NAMESPACE "reminders"
#define REMINDER_KEY "items"

ReminderManager::ReminderManager() { Load(); }

bool ReminderManager::AddAfterMinutes(uint32_t minutes, const std::string& text, uint32_t& id,
                                      std::string& error) {
    if (minutes == 0 || minutes > 525600) {
        error = "Thời gian nhắc phải từ 1 phút đến 1 năm";
        return false;
    }
    if (text.empty() || text.size() > kMaxTextBytes) {
        error = "Nội dung nhắc phải có từ 1 đến 120 byte";
        return false;
    }

    const auto now = static_cast<int64_t>(time(nullptr));
    if (now < 1704067200) {
        error = "Robo chưa có giờ chính xác; hãy kết nối Internet rồi thử lại";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (reminders_.size() >= kMaxReminders) {
        error = "Danh sách đã đầy (tối đa 10 lời nhắc). Hãy xem hoặc xóa lời nhắc cũ";
        return false;
    }

    Reminder reminder{next_id_, now + static_cast<int64_t>(minutes) * 60, text};
    reminders_.push_back(reminder);
    const auto old_next_id = next_id_;
    next_id_ = next_id_ == UINT32_MAX ? 1 : next_id_ + 1;
    if (!SaveLocked()) {
        reminders_.pop_back();
        next_id_ = old_next_id;
        error = "Không lưu được lời nhắc vào bộ nhớ";
        return false;
    }
    id = reminder.id;
    return true;
}

bool ReminderManager::Delete(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(reminders_.begin(), reminders_.end(),
                           [id](const Reminder& reminder) { return reminder.id == id; });
    if (it == reminders_.end()) {
        return false;
    }
    auto removed = *it;
    reminders_.erase(it);
    if (!SaveLocked()) {
        reminders_.push_back(std::move(removed));
        return false;
    }
    return true;
}

bool ReminderManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reminders_.empty()) {
        return true;
    }
    auto old_reminders = std::move(reminders_);
    reminders_.clear();
    if (!SaveLocked()) {
        reminders_ = std::move(old_reminders);
        return false;
    }
    return true;
}

std::string ReminderManager::List() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reminders_.empty()) {
        return "Không có lời nhắc nào đang chờ";
    }
    const auto now = static_cast<int64_t>(time(nullptr));
    auto sorted = reminders_;
    std::sort(sorted.begin(), sorted.end(),
              [](const Reminder& a, const Reminder& b) { return a.due_at < b.due_at; });
    std::string result = "Các lời nhắc đang chờ:";
    for (const auto& reminder : sorted) {
        const auto seconds_left = std::max<int64_t>(0, reminder.due_at - now);
        const auto minutes_left = (seconds_left + 59) / 60;
        result += "\n" + std::to_string(reminder.id) + ". Còn " +
                  std::to_string(minutes_left) + " phút - " + reminder.text;
    }
    return result;
}

std::optional<Reminder> ReminderManager::TakeDue(int64_t now) {
    if (now < 1704067200) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(reminders_.begin(), reminders_.end(),
                           [now](const Reminder& reminder) { return reminder.due_at <= now; });
    if (it == reminders_.end()) {
        return std::nullopt;
    }
    Reminder due = *it;
    reminders_.erase(it);
    if (!SaveLocked()) {
        reminders_.push_back(due);
        return std::nullopt;
    }
    if (due.due_at < now - 86400) {
        ESP_LOGW(TAG, "Discarding stale reminder %lu", static_cast<unsigned long>(due.id));
        return std::nullopt;
    }
    return due;
}

bool ReminderManager::SaveLocked() {
    cJSON* root = cJSON_CreateArray();
    if (root == nullptr) {
        return false;
    }
    for (const auto& reminder : reminders_) {
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr || !cJSON_AddNumberToObject(item, "id", reminder.id) ||
            !cJSON_AddNumberToObject(item, "due", static_cast<double>(reminder.due_at)) ||
            !cJSON_AddStringToObject(item, "text", reminder.text.c_str()) ||
            !cJSON_AddItemToArray(root, item)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return false;
        }
    }
    char* serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == nullptr) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(REMINDER_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, REMINDER_KEY, serialized);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    cJSON_free(serialized);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist reminders: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void ReminderManager::Load() {
    nvs_handle_t handle = 0;
    if (nvs_open(REMINDER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t length = 0;
    esp_err_t err = nvs_get_str(handle, REMINDER_KEY, nullptr, &length);
    if (err != ESP_OK || length == 0 || length > 4096) {
        nvs_close(handle);
        return;
    }
    std::string serialized(length, '\0');
    err = nvs_get_str(handle, REMINDER_KEY, serialized.data(), &length);
    nvs_close(handle);
    if (err != ESP_OK) {
        return;
    }
    cJSON* root = cJSON_Parse(serialized.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Ignoring invalid saved reminder list");
        return;
    }
    const auto now = static_cast<int64_t>(time(nullptr));
    for (cJSON* item = root->child; item != nullptr && reminders_.size() < kMaxReminders;
         item = item->next) {
        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* due = cJSON_GetObjectItem(item, "due");
        cJSON* text = cJSON_GetObjectItem(item, "text");
        if (!cJSON_IsNumber(id) || !cJSON_IsNumber(due) || !cJSON_IsString(text) ||
            text->valuestring == nullptr || std::strlen(text->valuestring) > kMaxTextBytes ||
            due->valuedouble < 1704067200) {
            continue;
        }
        Reminder reminder{static_cast<uint32_t>(id->valuedouble),
                          static_cast<int64_t>(due->valuedouble), text->valuestring};
        // Discard old one-shot reminders after a reboot; never play stale alerts days later.
        if (now >= 1704067200 && reminder.due_at < now - 86400) {
            continue;
        }
        reminders_.push_back(std::move(reminder));
        next_id_ =
            std::max(next_id_, reminders_.back().id == UINT32_MAX ? 1 : reminders_.back().id + 1);
    }
    cJSON_Delete(root);
}
