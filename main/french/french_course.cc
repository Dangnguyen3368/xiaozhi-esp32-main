#include "french_course.h"

#include "mcp_server.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#define TAG "FrenchCourse"

namespace {
constexpr const char* kNamespace = "french";

int Clamp(int lesson) {
    return std::max(0, std::min(kFrenchLessonCount - 1, lesson));
}

std::string ItemList(const FrenchItem* items, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (!out.empty()) {
            out += "; ";
        }
        out += items[i].fr;
        out += " = ";
        out += items[i].vi;
    }
    return out;
}

cJSON* Json(const std::string& key, const std::string& value) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, key.c_str(), value.c_str());
    return root;
}
}  // namespace

FrenchCourse& FrenchCourse::GetInstance() {
    static FrenchCourse instance;
    return instance;
}

FrenchCourse::FrenchCourse() {
    Load();
}

void FrenchCourse::Load() {
    Settings settings(kNamespace, false);
    current_ = Clamp(settings.GetInt("cur", 0));
    done_ = static_cast<uint32_t>(settings.GetInt("done_lo", 0)) |
            (static_cast<uint64_t>(static_cast<uint32_t>(settings.GetInt("done_hi", 0))) << 32);
    quiz_ok_ = settings.GetInt("q_ok", 0);
    quiz_total_ = settings.GetInt("q_all", 0);
    weak_count_ = 0;
    const std::string weak = settings.GetString("weak", "");
    size_t pos = 0;
    while (pos < weak.size() && weak_count_ < kMaxWeak) {
        size_t end = weak.find(',', pos);
        if (end == std::string::npos) {
            end = weak.size();
        }
        const int value = atoi(weak.substr(pos, end - pos).c_str());
        const int lesson = value >> 8;
        const int word = value & 0xFF;
        if (lesson >= 0 && lesson < kFrenchLessonCount && word < kFrenchLessons[lesson].word_count) {
            weak_[weak_count_++] = static_cast<uint16_t>(value);
        }
        pos = end + 1;
    }
}

void FrenchCourse::Save() const {
    Settings settings(kNamespace, true);
    settings.SetInt("cur", current_);
    settings.SetInt("done_lo", static_cast<int32_t>(done_ & 0xFFFFFFFF));
    settings.SetInt("done_hi", static_cast<int32_t>(done_ >> 32));
    settings.SetInt("q_ok", static_cast<int32_t>(quiz_ok_));
    settings.SetInt("q_all", static_cast<int32_t>(quiz_total_));
    std::string weak;
    for (int i = 0; i < weak_count_; ++i) {
        if (!weak.empty()) {
            weak += ',';
        }
        weak += std::to_string(weak_[i]);
    }
    settings.SetString("weak", weak);
}

void FrenchCourse::SetCurrent(int lesson) {
    current_ = Clamp(lesson);
    Save();
}

void FrenchCourse::MarkDone(int lesson) {
    lesson = Clamp(lesson);
    done_ |= (1ULL << lesson);
    Save();
}

bool FrenchCourse::IsDone(int lesson) const {
    return lesson >= 0 && lesson < kFrenchLessonCount && (done_ & (1ULL << lesson)) != 0;
}

int FrenchCourse::DoneCount() const {
    return __builtin_popcountll(done_);
}

int FrenchCourse::WeakCount() const {
    return weak_count_;
}

std::string FrenchCourse::ScreenTitle(int lesson) const {
    lesson = Clamp(lesson);
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "Bài %d/%d%s: %s", lesson + 1, kFrenchLessonCount, IsDone(lesson) ? " (xong)" : "",
             kFrenchLessons[lesson].title);
    return buffer;
}

std::string FrenchCourse::ScreenText(int lesson) const {
    const FrenchLesson& l = kFrenchLessons[Clamp(lesson)];
    std::string text = std::string("Unité ") + std::to_string(l.unit) + ": " + kFrenchUnitTitles[l.unit] + "\n";
    text += std::string(l.theme) + "\n\n";
    text += "TỪ MỚI\n";
    for (int i = 0; i < l.word_count; ++i) {
        text += std::string("• ") + l.words[i].fr + " = " + l.words[i].vi + "\n";
    }
    text += "\nMẪU CÂU\n";
    for (int i = 0; i < l.phrase_count; ++i) {
        text += std::string("• ") + l.phrases[i].fr + "\n   " + l.phrases[i].vi + "\n";
    }
    text += std::string("\nNGỮ PHÁP\n") + l.grammar + "\n";
    text += std::string("\nMỤC TIÊU\n") + l.goals;
    return text;
}

std::string FrenchCourse::LessonForModel(int lesson) const {
    lesson = Clamp(lesson);
    const FrenchLesson& l = kFrenchLessons[lesson];
    std::string text = "Lesson " + std::to_string(lesson + 1) + "/" + std::to_string(kFrenchLessonCount) +
                       " (unit " + std::to_string(l.unit) + " " + kFrenchUnitTitles[l.unit] + "): " + l.title +
                       ". Topic: " + l.theme + ". Goals: " + l.goals + ". Grammar: " + l.grammar +
                       ". Words: " + ItemList(l.words, l.word_count) +
                       ". Phrases: " + ItemList(l.phrases, l.phrase_count) + ".";
    text += IsDone(lesson) ? " The learner already finished this lesson." : "";
    text += lesson_viewer_ ? " The lesson is on the screen." : " This robot has no lesson screen: say the words clearly.";
    text += " Teach it step by step: say each French word slowly, "
            "give the Vietnamese meaning, let the learner repeat, then practise the phrases in a "
            "short role play. Explain grammar in Vietnamese. When the learner can use the phrases, "
            "call self.french.lesson with action `done`.";
    return text;
}

std::string FrenchCourse::ProgressForModel() const {
    std::string text = "Current lesson " + std::to_string(current_ + 1) + "/" + std::to_string(kFrenchLessonCount) +
                       " (" + kFrenchLessons[current_].title + "). Finished lessons: " +
                       std::to_string(DoneCount()) + ". Flash cards: " + std::to_string(quiz_ok_) + " right of " +
                       std::to_string(quiz_total_) + ". Words to review: " + std::to_string(weak_count_) + ".";
    if (weak_count_ > 0) {
        text += " Missed words:";
        for (int i = 0; i < weak_count_ && i < 10; ++i) {
            const FrenchItem& w = kFrenchLessons[weak_[i] >> 8].words[weak_[i] & 0xFF];
            text += std::string(" ") + w.fr + " (" + w.vi + ")";
            text += i + 1 < weak_count_ && i < 9 ? "," : ".";
        }
    }
    return text;
}

FrenchCourse::Quiz FrenchCourse::PickQuiz() {
    Quiz quiz;
    ++pick_counter_;
    // Every other card comes from the missed words while there are some.
    if (weak_count_ > 0 && (pick_counter_ % 2 == 0 || weak_count_ >= 8)) {
        const uint16_t value = weak_[esp_random() % weak_count_];
        quiz.lesson = value >> 8;
        quiz.word = value & 0xFF;
    } else {
        // Half of the cards from the current lesson, the rest from earlier ones.
        const int last = current_;
        quiz.lesson = (esp_random() % 2 == 0 || last == 0) ? last : static_cast<int>(esp_random() % (last + 1));
        quiz.word = esp_random() % kFrenchLessons[quiz.lesson].word_count;
    }
    const FrenchItem& item = kFrenchLessons[quiz.lesson].words[quiz.word];
    quiz.fr_to_vi = esp_random() % 3 != 0;
    quiz.question = quiz.fr_to_vi ? item.fr : item.vi;
    quiz.answer = quiz.fr_to_vi ? item.vi : item.fr;
    return quiz;
}

void FrenchCourse::RecordQuiz(int lesson, int word, bool correct) {
    if (lesson < 0 || lesson >= kFrenchLessonCount || word < 0 || word >= kFrenchLessons[lesson].word_count) {
        return;
    }
    const uint16_t value = static_cast<uint16_t>((lesson << 8) | word);
    auto* end = weak_ + weak_count_;
    auto* found = std::find(weak_, end, value);
    ++quiz_total_;
    if (correct) {
        ++quiz_ok_;
        if (found != end) {
            std::copy(found + 1, end, found);
            --weak_count_;
        }
    } else if (found == end) {
        if (weak_count_ == kMaxWeak) {
            std::copy(weak_ + 1, end, weak_);
            --weak_count_;
        }
        weak_[weak_count_++] = value;
    }
    Save();
}

void FrenchCourse::ShowLesson(int lesson) {
    SetCurrent(lesson);
    if (lesson_viewer_) {
        lesson_viewer_(current_);
    }
}

void FrenchCourse::ShowQuiz() {
    if (quiz_viewer_) {
        quiz_viewer_(PickQuiz());
    }
}

void FrenchCourse::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.french.lesson",
        "French course for a Vietnamese beginner (A1), 36 lessons following the units of the "
        "textbook 'Le nouveau Taxi ! 1'. Returns the lesson's words, phrases and grammar and shows "
        "them on screen. Actions: `current` (the lesson the learner is on), `go` (open lesson "
        "`number`, 1-36), `next`, `previous`, `done` (mark the current lesson finished and move "
        "to the next), `list` (titles of all lessons), `progress` (finished lessons, flash-card "
        "score and missed words). Use it whenever the learner wants to learn or review French.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("number", kPropertyTypeInteger, 0, 0, kFrenchLessonCount)}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto action = properties["action"].value<std::string>();
            const int number = properties["number"].value<int>();
            if (action == "list") {
                std::string list;
                for (int i = 0; i < kFrenchLessonCount; ++i) {
                    list += std::to_string(i + 1) + ". " + kFrenchLessons[i].title + " (" + kFrenchLessons[i].theme +
                            ")" + (IsDone(i) ? " [done]" : "") + "\n";
                }
                return Json("lessons", list);
            }
            if (action == "progress") {
                return Json("progress", ProgressForModel());
            }
            int lesson = current_;
            if (action == "go") {
                if (number < 1) {
                    return std::unexpected(std::string("number (1-36) is required for go"));
                }
                lesson = number - 1;
            } else if (action == "next") {
                lesson = current_ + 1;
            } else if (action == "previous") {
                lesson = current_ - 1;
            } else if (action == "done") {
                MarkDone(current_);
                const bool last = current_ + 1 >= kFrenchLessonCount;
                if (!last) {
                    ShowLesson(current_ + 1);
                }
                return Json("result", last ? std::string("Course finished: all 36 lessons done. Congratulate the learner.")
                                           : "Lesson marked done. Next: " + LessonForModel(current_));
            } else if (action != "current") {
                return std::unexpected(std::string("Unknown action. Use current, go, next, previous, done, list or progress."));
            }
            ShowLesson(lesson);
            return Json("lesson", LessonForModel(current_));
        });

    mcp.AddTool(
        "self.french.quiz",
        "Vocabulary flash cards for the French course. Action `ask` picks a word from the lessons "
        "learnt so far (missed words come back more often), shows it on screen and returns the "
        "question and the expected answer: ask the learner, do not reveal the answer first. Then "
        "call action `record` with the same `lesson` and `word` and `correct` true or false. "
        "Accept small spelling slips and answers without accents when speaking.",
        PropertyList({Property("action", kPropertyTypeString),
                      Property("lesson", kPropertyTypeInteger, 0, 0, kFrenchLessonCount),
                      Property("word", kPropertyTypeInteger, 0, 0, 255),
                      Property("correct", kPropertyTypeBoolean, false)}),
        [this](const PropertyList& properties) -> ToolResult {
            const auto action = properties["action"].value<std::string>();
            if (action == "ask") {
                const Quiz quiz = PickQuiz();
                if (quiz_viewer_) {
                    quiz_viewer_(quiz);
                }
                cJSON* root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "lesson", quiz.lesson + 1);
                cJSON_AddNumberToObject(root, "word", quiz.word);
                cJSON_AddStringToObject(root, "direction", quiz.fr_to_vi ? "French to Vietnamese" : "Vietnamese to French");
                cJSON_AddStringToObject(root, "question", quiz.question.c_str());
                cJSON_AddStringToObject(root, "answer", quiz.answer.c_str());
                return root;
            }
            if (action == "record") {
                const int lesson = properties["lesson"].value<int>() - 1;
                const int word = properties["word"].value<int>();
                const bool correct = properties["correct"].value<bool>();
                RecordQuiz(lesson, word, correct);
                return Json("result", std::string(correct ? "Saved as right. " : "Saved as missed; it will come back. ") +
                                          std::to_string(quiz_ok_) + " right of " + std::to_string(quiz_total_) + ".");
            }
            return std::unexpected(std::string("Use action ask or record."));
        });
    ESP_LOGI(TAG, "French course: %d lessons, current %d, done %d", kFrenchLessonCount, current_ + 1, DoneCount());
}
