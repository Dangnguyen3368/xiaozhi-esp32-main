#include "physics_tutor.h"

#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "study/physics_bank.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>

#include <cctype>
#include <cstdio>
#include <cstring>
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

// Set once a board registers a figure viewer, so problem JSON can tell the model.
bool g_figures_on_screen = false;

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
    return Learner{name, std::vector<uint8_t>(BitBytes(), 0), std::vector<uint8_t>(BitBytes(), 0)};
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
        figure_viewer_(p.figure, text);
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
        "progress), `progress` (per-topic progress and weak topics of the active learner). Ask the "
        "student's name at the start of a study session and select or add it.",
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
                    }
                }
                SaveNames();
            } else if (action != "list" && action != "progress") {
                return ErrorJson("Use action list, select, add, remove or progress.");
            }

            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "active", learners_[current_].name.c_str());
            if (action == "progress") {
                const auto& learner = learners_[current_];
                cJSON* topics = cJSON_AddArrayToObject(json, "topics");
                size_t total_done = 0;
                size_t total_wrong = 0;
                int weakest = -1;
                size_t weakest_wrong = 0;
                for (int t = 0; t < kTopicCount; ++t) {
                    size_t total = 0, done = 0, wrong = 0;
                    for (size_t i = 0; i < kPhysicsProblemCount; ++i) {
                        if (kPhysicsProblems[i].topic != t) {
                            continue;
                        }
                        ++total;
                        done += GetBit(learner.done, i);
                        wrong += GetBit(learner.wrong, i);
                    }
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "topic", kTopicNames[t]);
                    cJSON_AddNumberToObject(item, "done", done);
                    cJSON_AddNumberToObject(item, "total", total);
                    cJSON_AddNumberToObject(item, "need_review", wrong);
                    cJSON_AddItemToArray(topics, item);
                    total_done += done;
                    total_wrong += wrong;
                    if (wrong > weakest_wrong) {
                        weakest_wrong = wrong;
                        weakest = t;
                    }
                }
                cJSON_AddNumberToObject(json, "done", total_done);
                cJSON_AddNumberToObject(json, "total", kPhysicsProblemCount);
                cJSON_AddNumberToObject(json, "need_review", total_wrong);
                if (weakest >= 0) {
                    cJSON_AddStringToObject(json, "weakest_topic", kTopicNames[weakest]);
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
        "automatic by progress. `mode`: new (not yet done), review (previously wrong), id (the "
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
                pool = Candidates(topic, level, false, true);
                if (pool.empty()) {
                    return ErrorJson("Nothing to review: the student has no wrong problems here.");
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
        "otherwise. Wrong problems come back in review mode.",
        PropertyList({Property("id", kPropertyTypeString), Property("correct", kPropertyTypeBoolean)}),
        [this](const PropertyList& properties) -> ToolResult {
            const int index = FindProblem(properties["id"].value<std::string>());
            if (index < 0) {
                return ErrorJson("No problem with that id.");
            }
            const bool correct = properties["correct"].value<bool>();
            auto& learner = Current();
            SetBit(learner.done, index, true);
            SetBit(learner.wrong, index, !correct);
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
}
