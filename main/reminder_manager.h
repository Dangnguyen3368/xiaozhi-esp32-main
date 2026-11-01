#ifndef REMINDER_MANAGER_H
#define REMINDER_MANAGER_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct Reminder {
    uint32_t id;
    int64_t due_at;
    std::string text;
};

class ReminderManager {
public:
    static constexpr size_t kMaxReminders = 10;
    static constexpr size_t kMaxTextBytes = 120;

    static ReminderManager& GetInstance() {
        static ReminderManager instance;
        return instance;
    }

    ReminderManager();
    bool AddAfterMinutes(uint32_t minutes, const std::string& text, uint32_t& id,
                         std::string& error);
    bool Delete(uint32_t id);
    bool Clear();
    std::string List() const;
    std::optional<Reminder> TakeDue(int64_t now);

private:
    bool SaveLocked();
    void Load();

    mutable std::mutex mutex_;
    std::vector<Reminder> reminders_;
    uint32_t next_id_ = 1;
};

#endif
