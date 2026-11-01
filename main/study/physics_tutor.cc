#include "physics_tutor.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "study/physics_bank.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <strings.h>

#define TAG "PhysicsTutor"

namespace {
constexpr size_t kMaxLearners = 6;
constexpr char kNamespace[] = "study";
constexpr char kDefaultLearner[] = "Học sinh";

const char* const kTopicKeys[kTopicCount] = {"co", "nhiet", "dien", "quang", "thuchanh"};
const char* const kTopicNames[kTopicCount] = {"Cơ học", "Nhiệt học", "Điện học", "Quang học",
                                              "Thực hành"};
const char* const kLevelNames[4] = {"tự chọn", "cơ bản", "khá", "chuyên"};

// Days until a reviewed problem comes back, by box. A correct answer in the last box takes the
// problem out of the review list.
constexpr int kReviewIntervalDays[] = {1, 2, 4, 8};
constexpr int kReviewBoxes = sizeof(kReviewIntervalDays) / sizeof(kReviewIntervalDays[0]);
constexpr size_t kMaxReviewItems = 200;  // keeps the NVS string well under its size limit
constexpr int kDefaultReviewMinute = 20 * 60;
// After the set time, keep trying (the robot may be busy) for this long before giving up today.
constexpr int kReviewWindowMinutes = 60;
constexpr int kDailyReviewProblems = 3;

// Set once a board registers a figure viewer, so problem JSON can tell the model.
bool g_figures_on_screen = false;

// Local time: the OTA server sets the clock to local wall time, so UTC fields are local.
// Returns false until the clock has been set.
bool LocalNow(int32_t& day, int& minute_of_day) {
    const time_t now = time(nullptr);
    if (now < 1700000000) {
        return false;
    }
    day = static_cast<int32_t>(now / 86400);
    minute_of_day = static_cast<int>((now % 86400) / 60);
    return true;
}

// Today's day number, or -1 while the clock is unknown (then every review counts as due).
int32_t Today() {
    int32_t day = 0;
    int minute = 0;
    return LocalNow(day, minute) ? day : -1;
}

std::string ReviewToString(const std::vector<PhysicsTutor::ReviewItem>& items) {
    std::string out;
    for (const auto& item : items) {
        out += std::to_string(item.index) + ":" + std::to_string(item.box) + ":" +
               std::to_string(item.due_day) + ";";
    }
    return out;
}

std::vector<PhysicsTutor::ReviewItem> ReviewFromString(const std::string& text) {
    std::vector<PhysicsTutor::ReviewItem> items;
    size_t start = 0;
    while (start < text.size() && items.size() < kMaxReviewItems) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        unsigned index = 0, box = 0;
        long due_day = 0;
        if (sscanf(text.substr(start, end - start).c_str(), "%u:%u:%ld", &index, &box, &due_day) == 3 &&
            index < kPhysicsProblemCount && box < kReviewBoxes) {
            items.push_back({static_cast<uint16_t>(index), static_cast<uint8_t>(box),
                             static_cast<int32_t>(due_day)});
        }
        start = end + 1;
    }
    return items;
}

// NVS key for a learner slot, e.g. "d0" (done bits) or "w0" (wrong bits).
std::string SlotKey(char prefix, size_t slot) { return std::string(1, prefix) + std::to_string(slot); }

size_t BitBytes() { return (kPhysicsProblemCount + 7) / 8; }

bool GetBit(const std::vector<uint8_t>& bits, size_t i) {
    return i / 8 < bits.size() && ((bits[i / 8] >> (i % 8)) & 1) != 0;
}

void SetBit(std::vector<uint8_t>& bits, size_t i, bool on) {
    if (i / 8 >= bits.size()) {
        return;
    }
    if (on) {
        bits[i / 8] |= 1 << (i % 8);
    } else {
        bits[i / 8] &= ~(1 << (i % 8));
    }
}

std::string ToHex(const std::vector<uint8_t>& bits) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bits.size() * 2);
    for (uint8_t b : bits) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0f]);
    }
    return out;
}

// Unknown or short strings decode to zero bits, so a bank that grew keeps old progress.
std::vector<uint8_t> FromHex(const std::string& hex, size_t size) {
    std::vector<uint8_t> bits(size, 0);
    for (size_t i = 0; i < size && i * 2 + 1 < hex.size(); ++i) {
        unsigned value = 0;
        if (sscanf(hex.c_str() + i * 2, "%2x", &value) == 1) {
            bits[i] = static_cast<uint8_t>(value);
        }
    }
    return bits;
}

std::string Trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Returns -1 for "any" or an empty string, -2 for an unknown topic.
int ParseTopic(const std::string& topic) {
    if (topic.empty() || topic == "any") {
        return -1;
    }
    for (int i = 0; i < kTopicCount; ++i) {
        if (topic == kTopicKeys[i]) {
            return i;
        }
    }
    return -2;
}

cJSON* ProblemJson(size_t index) {
    const auto& p = kPhysicsProblems[index];
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", p.id);
    cJSON_AddStringToObject(json, "topic", kTopicNames[p.topic]);
    cJSON_AddNumberToObject(json, "level", p.level);
    cJSON_AddStringToObject(json, "level_name", kLevelNames[p.level]);
    cJSON_AddStringToObject(json, "title", p.title);
    cJSON_AddStringToObject(json, "question", p.question);
    cJSON_AddNumberToObject(json, "hints_available", p.hint_count);
    if (p.figure != nullptr && p.figure->has_drawing) {
        cJSON_AddStringToObject(json, "figure",
                                g_figures_on_screen
                                    ? "The figure and a summary of the data are shown on the device "
                                      "screen. Tell the student to look at it."
                                    : "This device has no colour screen; describe the figure from the question text.");
    } else if (p.figure != nullptr && g_figures_on_screen) {
        cJSON_AddStringToObject(json, "screen",
                                "A summary of the given data is shown on the device screen; the "
                                "student can read the numbers there.");
    }
    return json;
}

cJSON* ErrorJson(const std::string& message) {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", message.c_str());
    return json;
}
}  // namespace

PhysicsTutor& PhysicsTutor::GetInstance() {
    static PhysicsTutor instance;
    return instance;
}

PhysicsTutor::Learner PhysicsTutor::MakeLearner(const std::string& name) const {
    return Learner{name, std::vector<uint8_t>(BitBytes(), 0), std::vector<uint8_t>(BitBytes(), 0), {}};
}

void PhysicsTutor::Load() {
    if (loaded_) {
        return;
    }
    loaded_ = true;
    Settings settings(kNamespace);
    const std::string names = settings.GetString("names");
    size_t start = 0;
    while (start <= names.size() && learners_.size() < kMaxLearners) {
        const size_t end = names.find('\n', start);
        const std::string name = Trim(names.substr(start, end == std::string::npos ? end : end - start));
        if (!name.empty()) {
            const size_t i = learners_.size();
            Learner learner = MakeLearner(name);
            learner.done = FromHex(settings.GetString(SlotKey('d', i)), BitBytes());
            learner.wrong = FromHex(settings.GetString(SlotKey('w', i)), BitBytes());
            learner.review = ReviewFromString(settings.GetString(SlotKey('r', i)));
            // Wrong problems from before spaced review existed: due now, first box.
            for (size_t p = 0; p < kPhysicsProblemCount; ++p) {
                const bool listed = std::any_of(learner.review.begin(), learner.review.end(),
                                                [p](const ReviewItem& item) { return item.index == p; });
                if (GetBit(learner.wrong, p) && !listed && learner.review.size() < kMaxReviewItems) {
                    learner.review.push_back({static_cast<uint16_t>(p), 0, 0});
                }
            }
            learners_.push_back(std::move(learner));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (learners_.empty()) {
        learners_.push_back(MakeLearner(kDefaultLearner));
    }
    current_ = static_cast<size_t>(settings.GetInt("cur", 0));
    if (current_ >= learners_.size()) {
        current_ = 0;
    }
    review_minute_ = settings.GetInt("rev_min", kDefaultReviewMinute);
    review_fired_day_ = settings.GetInt("rev_day", -1);
    ESP_LOGI(TAG, "Loaded %u learner(s), bank has %u problems",
             static_cast<unsigned>(learners_.size()), static_cast<unsigned>(kPhysicsProblemCount));
}

void PhysicsTutor::SaveNames() {
    std::string names;
    for (const auto& learner : learners_) {
        if (!names.empty()) {
            names.push_back('\n');
        }
        names += learner.name;
    }
    Settings settings(kNamespace, true);
    settings.SetString("names", names);
    settings.SetInt("cur", static_cast<int32_t>(current_));
}

void PhysicsTutor::SaveLearner(size_t index) {
    if (index >= learners_.size()) {
        return;
    }
    Settings settings(kNamespace, true);
    settings.SetString(SlotKey('d', index), ToHex(learners_[index].done));
    settings.SetString(SlotKey('w', index), ToHex(learners_[index].wrong));
    settings.SetString(SlotKey('r', index), ReviewToString(learners_[index].review));
}

PhysicsTutor::Learner& PhysicsTutor::Current() {
    Load();
    return learners_[current_];
}

int PhysicsTutor::FindLearner(const std::string& name) const {
    for (size_t i = 0; i < learners_.size(); ++i) {
        if (learners_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int PhysicsTutor::FindProblem(const std::string& id) const {
    // Speech recognition writes ids loosely ("D 115", "d-115", "D1"): keep letters and digits,
    // then pad the number to the bank's two-digit minimum.
    std::string letters;
    std::string digits;
    for (char c : id) {
        if (isdigit(static_cast<unsigned char>(c))) {
            digits += c;
        } else if (isalpha(static_cast<unsigned char>(c)) && digits.empty()) {
            letters += static_cast<char>(toupper(static_cast<unsigned char>(c)));
        }
    }
    while (!digits.empty() && digits.size() < 2) {
        digits.insert(digits.begin(), '0');
    }
    const std::string key = letters + digits;
    for (size_t i = 0; i < kPhysicsProblemCount; ++i) {
        if (strcasecmp(kPhysicsProblems[i].id, key.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<size_t> PhysicsTutor::Candidates(int topic, int level, bool only_new, bool only_wrong) {
    const auto& learner = Current();
    std::vector<size_t> result;
    for (size_t i = 0; i < kPhysicsProblemCount; ++i) {
        const auto& p = kPhysicsProblems[i];
        if ((topic >= 0 && p.topic != topic) || (level > 0 && p.level != level)) {
            continue;
        }
        if ((only_new && GetBit(learner.done, i)) || (only_wrong && !GetBit(learner.wrong, i))) {
            continue;
        }
        result.push_back(i);
    }
    return result;
}

std::vector<size_t> PhysicsTutor::DueReviews(const Learner& learner, int topic) const {
    const int32_t today = Today();
    std::vector<ReviewItem> due;
    for (const auto& item : learner.review) {
        if ((today < 0 || item.due_day <= today) &&
            (topic < 0 || kPhysicsProblems[item.index].topic == topic)) {
            due.push_back(item);
        }
    }
    // Most overdue first, then the ones the student knows least.
    std::sort(due.begin(), due.end(), [](const ReviewItem& a, const ReviewItem& b) {
        return a.due_day != b.due_day ? a.due_day < b.due_day : a.box < b.box;
    });
    std::vector<size_t> result;
    for (const auto& item : due) {
        result.push_back(item.index);
    }
    return result;
}

void PhysicsTutor::RecordResult(Learner& learner, size_t index, bool correct) {
    SetBit(learner.done, index, true);
    const int32_t today = std::max<int32_t>(Today(), 0);
    auto it = std::find_if(learner.review.begin(), learner.review.end(),
                           [index](const ReviewItem& item) { return item.index == index; });
    if (!correct) {
        if (it == learner.review.end()) {
            if (learner.review.size() >= kMaxReviewItems) {
                learner.review.erase(learner.review.begin());
            }
            learner.review.push_back({static_cast<uint16_t>(index), 0, 0});
            it = learner.review.end() - 1;
        }
        it->box = 0;
        it->due_day = today + kReviewIntervalDays[0];
        SetBit(learner.wrong, index, true);
        return;
    }
    if (it == learner.review.end()) {
        SetBit(learner.wrong, index, false);
        return;
    }
    if (it->box + 1 >= kReviewBoxes) {
        learner.review.erase(it);
        SetBit(learner.wrong, index, false);
        return;
    }
    it->box += 1;
    it->due_day = today + kReviewIntervalDays[it->box];
}

void PhysicsTutor::SetProgressViewer(ProgressViewer viewer) { progress_viewer_ = std::move(viewer); }

PhysicsTutor::ProgressReport PhysicsTutor::ShowProgress() {
    const auto& learner = Current();
    ProgressReport report;
    report.learner = learner.name;
    for (int t = 0; t < kTopicCount; ++t) {
        TopicProgress topic{kTopicNames[t], 0, 0, 0};
        for (size_t i = 0; i < kPhysicsProblemCount; ++i) {
            if (kPhysicsProblems[i].topic != t) {
                continue;
            }
            ++topic.total;
            topic.done += GetBit(learner.done, i);
            topic.need_review += GetBit(learner.wrong, i);
        }
        report.done += topic.done;
        report.total += topic.total;
        report.need_review += topic.need_review;
        report.topics.push_back(topic);
    }
    report.due_today = static_cast<int>(DueReviews(learner, -1).size());
    if (progress_viewer_) {
        progress_viewer_(report);
    }
    return report;
}

bool PhysicsTutor::ShowFormulas(const std::string& topic) {
    const int t = ParseTopic(topic);
    for (size_t i = 0; i < kPhysicsFormulaCount; ++i) {
        if (kPhysicsFormulas[i].topic == t) {
            if (figure_viewer_) {
                figure_viewer_(kPhysicsFormulas[i].image, kPhysicsFormulas[i].title, ScreenKind::kFormulas);
            }
            return true;
        }
    }
    return false;
}

void PhysicsTutor::StartDailyReviewTimer() {
    if (review_timer_ != nullptr) {
        return;
    }
    esp_timer_create_args_t args = {};
    args.callback = [](void* arg) {
        // Runs in the esp_timer task: hand the work to the main task.
        Application::GetInstance().Schedule(
            [arg]() { static_cast<PhysicsTutor*>(arg)->CheckDailyReview(); });
    };
    args.arg = this;
    args.name = "daily_review";
    if (esp_timer_create(&args, &review_timer_) == ESP_OK) {
        esp_timer_start_periodic(review_timer_, 30ULL * 1000 * 1000);
    }
}

void PhysicsTutor::CheckDailyReview() {
    Load();
    int32_t day = 0;
    int minute = 0;
    if (review_minute_ < 0 || !LocalNow(day, minute) || day == review_fired_day_) {
        return;
    }
    if (minute < review_minute_ || minute > review_minute_ + kReviewWindowMinutes) {
        return;
    }
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle) {
        return;  // busy: try again on the next check
    }
    const auto& learner = Current();
    const size_t due = DueReviews(learner, -1).size();
    review_fired_day_ = day;
    {
        Settings settings(kNamespace, true);
        settings.SetInt("rev_day", review_fired_day_);
    }
    if (due == 0) {
        ESP_LOGI(TAG, "Daily review: nothing due for %s", learner.name.c_str());
        return;
    }
    // The name goes into a JSON string on the wire; keep it to plain text.
    std::string name;
    for (char c : learner.name) {
        if (c != '"' && c != '\\') {
            name += c;
        }
    }
    const int count = std::min<int>(static_cast<int>(due), kDailyReviewProblems);
    ESP_LOGI(TAG, "Daily review: %u due for %s", static_cast<unsigned>(due), name.c_str());
    if (auto* display = Board::GetInstance().GetDisplay()) {
        display->ShowNotification("Đến giờ ôn bài sai", 10000);
    }
    app.WakeWordInvoke("Đến giờ ôn bài rồi. Hãy cho " + name + " ôn " + std::to_string(count) +
                       " bài sai đến hạn hôm nay, lấy bằng chế độ review.");
}

void PhysicsTutor::SetFigureViewer(FigureViewer viewer) {
    figure_viewer_ = std::move(viewer);
    g_figures_on_screen = static_cast<bool>(figure_viewer_);
}

void PhysicsTutor::ShowOnScreen(size_t index) {
    const auto& p = kPhysicsProblems[index];
    const std::string text = std::string("Bài ") + p.id + " · " + kTopicNames[p.topic] + " · " +
                             kLevelNames[p.level];
    if (figure_viewer_) {
        // Also called with nullptr so the previous problem's figure goes away.
        figure_viewer_(p.figure, text, ScreenKind::kProblem);
    }
    auto* display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    display->ShowNotification(text.c_str(), 8000);
}

void PhysicsTutor::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.study.learner",
        "Manage learners for physics exam practice (luyện thi vào 10 chuyên Vật lý). Each learner "
        "has separate progress. Actions: `list` (all learners and who is active), `select` (switch "
        "to `name`), `add` (create `name` and switch to it), `remove` (delete `name` and its "
        "progress), `progress` (per-topic progress and weak topics of the active learner; also "
        "draws a chart on colour screens). Ask the student's name at the start of a study session "
        "and select or add it.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("name", kPropertyTypeString, std::string(""))}),
        [this](const PropertyList& properties) -> ToolResult {
            Load();
            const auto action = properties["action"].value<std::string>();
            const auto name = Trim(properties["name"].value<std::string>());
            if (action == "select" || action == "add") {
                if (name.empty()) {
                    return ErrorJson("name is required");
                }
                int index = FindLearner(name);
                if (index < 0) {
                    if (action == "select") {
                        return ErrorJson("No learner with that name. Use action `add` to create it.");
                    }
                    // Replace the unused default learner instead of keeping it around.
                    if (learners_.size() == 1 && learners_[0].name == kDefaultLearner &&
                        Candidates(-1, 0, false, false).size() == Candidates(-1, 0, true, false).size()) {
                        learners_[0] = MakeLearner(name);
                        index = 0;
                    } else if (learners_.size() >= kMaxLearners) {
                        return ErrorJson("At most 6 learners. Remove one first.");
                    } else {
                        learners_.push_back(MakeLearner(name));
                        index = static_cast<int>(learners_.size() - 1);
                    }
                    SaveLearner(index);
                }
                current_ = static_cast<size_t>(index);
                SaveNames();
            } else if (action == "remove") {
                const int index = FindLearner(name);
                if (index < 0) {
                    return ErrorJson("No learner with that name.");
                }
                learners_.erase(learners_.begin() + index);
                if (learners_.empty()) {
                    learners_.push_back(MakeLearner(kDefaultLearner));
                }
                current_ = 0;
                // Rewrite every slot so the NVS keys match the new positions.
                for (size_t i = 0; i < kMaxLearners; ++i) {
                    if (i < learners_.size()) {
                        SaveLearner(i);
                    } else {
                        Settings settings(kNamespace, true);
                        settings.EraseKey(SlotKey('d', i));
                        settings.EraseKey(SlotKey('w', i));
                        settings.EraseKey(SlotKey('r', i));
                    }
                }
                SaveNames();
            } else if (action != "list" && action != "progress") {
                return ErrorJson("Use action list, select, add, remove or progress.");
            }

            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "active", learners_[current_].name.c_str());
            if (action == "progress") {
                const auto report = ShowProgress();
                cJSON* topics = cJSON_AddArrayToObject(json, "topics");
                int weakest = -1;
                int weakest_wrong = 0;
                for (size_t t = 0; t < report.topics.size(); ++t) {
                    const auto& topic = report.topics[t];
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "topic", topic.name);
                    cJSON_AddNumberToObject(item, "done", topic.done);
                    cJSON_AddNumberToObject(item, "total", topic.total);
                    cJSON_AddNumberToObject(item, "need_review", topic.need_review);
                    cJSON_AddItemToArray(topics, item);
                    if (topic.need_review > weakest_wrong) {
                        weakest_wrong = topic.need_review;
                        weakest = static_cast<int>(t);
                    }
                }
                cJSON_AddNumberToObject(json, "done", report.done);
                cJSON_AddNumberToObject(json, "total", report.total);
                cJSON_AddNumberToObject(json, "need_review", report.need_review);
                cJSON_AddNumberToObject(json, "due_for_review_today", report.due_today);
                if (weakest >= 0) {
                    cJSON_AddStringToObject(json, "weakest_topic", report.topics[weakest].name);
                }
                if (progress_viewer_) {
                    cJSON_AddStringToObject(json, "screen", "A progress chart is shown on the device screen.");
                }
            } else {
                cJSON* names = cJSON_AddArrayToObject(json, "learners");
                for (const auto& learner : learners_) {
                    cJSON_AddItemToArray(names, cJSON_CreateString(learner.name.c_str()));
                }
            }
            return json;
        });

    mcp.AddTool(
        "self.study.get_problem",
        "Get a physics problem from the built-in bank for grade-10 specialized exam practice "
        "(Lam Son, Thanh Hoa). Use it whenever the student wants to practise physics, instead of "
        "inventing problems. `topic`: co (mechanics), nhiet (heat), dien (electricity), quang "
        "(optics), thuchanh (experiments) or any. `level`: 1 basic, 2 good, 3 specialized, 0 = "
        "automatic by progress. `mode`: new (not yet done), review (previously wrong; spaced review "
        "picks the most overdue first), id (the "
        "problem `id`), exam (a 5-problem mock exam, one per topic). When the student names a problem, "
        "such as D115, always pass it as `id`. Read the question aloud in "
        "Vietnamese, reading units and formulas in words, then wait for the student's answer. Do "
        "not reveal the answer before the student tries.",
        PropertyList({Property("topic", kPropertyTypeString, std::string("any")),
                      Property("level", kPropertyTypeInteger, 0, 0, 3),
                      Property("mode", kPropertyTypeString, std::string("new")),
                      Property("id", kPropertyTypeString, std::string(""))}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto mode = properties["mode"].value<std::string>();
            const int level = properties["level"].value<int>();
            const int topic = ParseTopic(properties["topic"].value<std::string>());
            if (topic == -2) {
                return ErrorJson("Unknown topic. Use co, nhiet, dien, quang, thuchanh or any.");
            }
            const auto& learner_name = Current().name;

            // A non-empty id always wins: models often pass it but leave mode at "new".
            const auto id = properties["id"].value<std::string>();
            if (mode == "id" || !id.empty()) {
                const int index = FindProblem(id);
                ESP_LOGI(TAG, "get_problem id '%s' -> %d", id.c_str(), index);
                if (index < 0) {
                    return ErrorJson("No problem with that id.");
                }
                ShowOnScreen(index);
                return ProblemJson(index);
            }

            if (mode == "exam") {
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "learner", learner_name.c_str());
                cJSON_AddStringToObject(json, "instructions",
                                        "Give these problems one by one. Record each result. At "
                                        "the end, give a score out of 10 (2 points per problem).");
                cJSON* list = cJSON_AddArrayToObject(json, "problems");
                for (int t = 0; t < kTopicCount; ++t) {
                    auto pool = Candidates(t, 3, true, false);
                    if (pool.empty()) {
                        pool = Candidates(t, 2, true, false);
                    }
                    if (pool.empty()) {
                        pool = Candidates(t, 3, false, false);
                    }
                    if (!pool.empty()) {
                        cJSON_AddItemToArray(list, ProblemJson(pool[esp_random() % pool.size()]));
                    }
                }
                return json;
            }

            std::vector<size_t> pool;
            const char* note = nullptr;
            if (mode == "review") {
                // Spaced review: the most overdue problem first, then any other wrong one.
                pool = DueReviews(Current(), topic);
                if (!pool.empty()) {
                    pool.resize(1);
                } else {
                    pool = Candidates(topic, level, false, true);
                    if (pool.empty()) {
                        return ErrorJson("Nothing to review: the student has no wrong problems here.");
                    }
                    note = "Nothing is due for review today; this wrong problem comes early.";
                }
            } else if (mode == "new") {
                if (level == 0) {
                    for (int lv = 1; lv <= 3 && pool.empty(); ++lv) {
                        pool = Candidates(topic, lv, true, false);
                    }
                } else {
                    pool = Candidates(topic, level, true, false);
                }
                if (pool.empty()) {
                    pool = Candidates(topic, level, false, false);
                    note = "The student has done every problem here; this one is a repeat.";
                }
            } else {
                return ErrorJson("Use mode new, review, id or exam.");
            }
            if (pool.empty()) {
                return ErrorJson("No problem matches.");
            }
            const size_t index = pool[esp_random() % pool.size()];
            ShowOnScreen(index);
            cJSON* json = ProblemJson(index);
            cJSON_AddStringToObject(json, "learner", learner_name.c_str());
            if (note != nullptr) {
                cJSON_AddStringToObject(json, "note", note);
            }
            return json;
        });

    mcp.AddTool(
        "self.study.reveal",
        "Get part of a problem's key. `part`: hint (hint number `index`, starting at 1), answer "
        "(the correct final answer, for grading only after the student has answered; do not read "
        "it out unless they give up), or solution (step-by-step solution, explain it in your own "
        "words). When the student is wrong, give hints first, one at a time.",
        PropertyList({Property("id", kPropertyTypeString), Property("part", kPropertyTypeString),
                      Property("index", kPropertyTypeInteger, 1, 1, 3)}),
        [this](const PropertyList& properties) -> ToolResult {
            const int index = FindProblem(properties["id"].value<std::string>());
            if (index < 0) {
                return ErrorJson("No problem with that id.");
            }
            const auto& p = kPhysicsProblems[index];
            const auto part = properties["part"].value<std::string>();
            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "id", p.id);
            if (part == "hint") {
                const int n = properties["index"].value<int>();
                if (n > p.hint_count) {
                    cJSON_Delete(json);
                    return ErrorJson("No more hints. Explain the solution instead.");
                }
                cJSON_AddStringToObject(json, "hint", p.hints[n - 1]);
                cJSON_AddNumberToObject(json, "hints_left", p.hint_count - n);
            } else if (part == "answer") {
                cJSON_AddStringToObject(json, "answer", p.answer);
            } else if (part == "solution") {
                cJSON_AddStringToObject(json, "answer", p.answer);
                cJSON_AddStringToObject(json, "solution", p.solution);
            } else {
                cJSON_Delete(json);
                return ErrorJson("Use part hint, answer or solution.");
            }
            return json;
        });

    mcp.AddTool(
        "self.study.record",
        "Save the result of a problem for the active learner after grading: `correct` true if "
        "the student's final answer matches the key (equivalent values and units count), false "
        "otherwise. Wrong problems come back in review mode, spaced further apart (1, 2, 4, 8 days) "
        "each time the student gets them right, until they are learned.",
        PropertyList({Property("id", kPropertyTypeString), Property("correct", kPropertyTypeBoolean)}),
        [this](const PropertyList& properties) -> ToolResult {
            const int index = FindProblem(properties["id"].value<std::string>());
            if (index < 0) {
                return ErrorJson("No problem with that id.");
            }
            const bool correct = properties["correct"].value<bool>();
            auto& learner = Current();
            RecordResult(learner, index, correct);
            SaveLearner(current_);
            size_t done = 0;
            for (size_t i = 0; i < kPhysicsProblemCount; ++i) {
                done += GetBit(learner.done, i);
            }
            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "learner", learner.name.c_str());
            cJSON_AddNumberToObject(json, "done", done);
            cJSON_AddNumberToObject(json, "total", kPhysicsProblemCount);
            return json;
        });

    mcp.AddTool(
        "self.study.formulas",
        "Show the formula sheet (thẻ công thức) of a physics topic: co (mechanics), nhiet (heat), "
        "dien (electricity) or quang (optics). Use it when the student asks for formulas. The "
        "sheet appears on colour screens; the formulas also come back as text so you can read or "
        "explain the ones the student needs.",
        PropertyList({Property("topic", kPropertyTypeString)}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto topic = properties["topic"].value<std::string>();
            const int t = ParseTopic(topic);
            for (size_t i = 0; i < kPhysicsFormulaCount; ++i) {
                if (kPhysicsFormulas[i].topic != t) {
                    continue;
                }
                ShowFormulas(topic);
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "title", kPhysicsFormulas[i].title);
                cJSON_AddStringToObject(json, "formulas", kPhysicsFormulas[i].text);
                if (g_figures_on_screen) {
                    cJSON_AddStringToObject(json, "screen", "The formula sheet is shown on the device screen.");
                }
                return json;
            }
            return ErrorJson("Use topic co, nhiet, dien or quang.");
        });

    mcp.AddTool(
        "self.study.daily_review",
        "Daily review reminder (ôn bài sai hằng ngày). Every day at the set time the robot reminds "
        "the active learner to review the wrong problems that are due. Actions: `status`, `set` "
        "(set `time` as HH:MM, 24-hour), `off`, `due` (list the problems due today for the active "
        "learner). When reminded, give the due problems one by one with get_problem mode review.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("time", kPropertyTypeString, std::string(""))}),
        [this](const PropertyList& properties) -> ToolResult {
            Load();
            const auto action = properties["action"].value<std::string>();
            if (action == "set") {
                int hour = -1, minute = -1;
                const auto time_text = properties["time"].value<std::string>();
                if (sscanf(time_text.c_str(), "%d:%d", &hour, &minute) != 2 || hour < 0 || hour > 23 ||
                    minute < 0 || minute > 59) {
                    return ErrorJson("Give time as HH:MM, for example 20:00.");
                }
                review_minute_ = hour * 60 + minute;
                review_fired_day_ = -1;
            } else if (action == "off") {
                review_minute_ = -1;
            } else if (action != "status" && action != "due") {
                return ErrorJson("Use action status, set, off or due.");
            }
            if (action == "set" || action == "off") {
                Settings settings(kNamespace, true);
                settings.SetInt("rev_min", review_minute_);
                settings.SetInt("rev_day", review_fired_day_);
            }
            cJSON* json = cJSON_CreateObject();
            if (review_minute_ >= 0) {
                char text[8];
                snprintf(text, sizeof(text), "%02d:%02d", review_minute_ / 60, review_minute_ % 60);
                cJSON_AddStringToObject(json, "reminder_time", text);
            } else {
                cJSON_AddStringToObject(json, "reminder_time", "off");
            }
            const auto& learner = Current();
            const auto due = DueReviews(learner, -1);
            cJSON_AddStringToObject(json, "learner", learner.name.c_str());
            cJSON_AddNumberToObject(json, "due_today", due.size());
            if (action == "due") {
                cJSON* list = cJSON_AddArrayToObject(json, "problems");
                for (size_t i = 0; i < due.size() && i < static_cast<size_t>(kDailyReviewProblems); ++i) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "id", kPhysicsProblems[due[i]].id);
                    cJSON_AddStringToObject(item, "title", kPhysicsProblems[due[i]].title);
                    cJSON_AddItemToArray(list, item);
                }
            }
            return json;
        });

    StartDailyReviewTimer();
}
