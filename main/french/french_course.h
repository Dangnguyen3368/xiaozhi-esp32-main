#ifndef FRENCH_COURSE_H
#define FRENCH_COURSE_H

#include <cstdint>
#include <functional>
#include <string>

// French course for Vietnamese beginners (A1), French tutor edition only
// (CONFIG_XIAOZHI_FRENCH_EDITION). The 36 lessons follow the units and topics of the textbook
// "Le nouveau Taxi ! 1" (Hachette); vocabulary, phrases and explanations are our own.

struct FrenchItem {
    const char* fr;
    const char* vi;
};

struct FrenchLesson {
    uint8_t unit;           // 1..9
    const char* title;      // French title
    const char* theme;      // topic in Vietnamese
    const char* goals;      // what the learner can do afterwards (Vietnamese)
    const char* grammar;    // grammar and pronunciation points
    const FrenchItem* words;
    uint8_t word_count;
    const FrenchItem* phrases;
    uint8_t phrase_count;
};

extern const FrenchLesson kFrenchLessons[];
extern const int kFrenchLessonCount;
extern const char* const kFrenchUnitTitles[];  // index 1..9
extern const int kFrenchUnitCount;

class FrenchCourse {
public:
    static FrenchCourse& GetInstance();

    // Registers the self.french.* MCP tools. Call once from the board's InitializeTools().
    void RegisterTools();

    struct Quiz {
        int lesson = -1;     // 0-based lesson index
        int word = -1;       // index in the lesson's word list
        bool fr_to_vi = true;
        std::string question;
        std::string answer;
    };

    // Boards with a colour screen show lessons and flash cards through these callbacks.
    using LessonViewer = std::function<void(int lesson)>;
    using QuizViewer = std::function<void(const Quiz& quiz)>;
    void SetLessonViewer(LessonViewer viewer) { lesson_viewer_ = std::move(viewer); }
    void SetQuizViewer(QuizViewer viewer) { quiz_viewer_ = std::move(viewer); }

    int current() const { return current_; }
    // Makes `lesson` (0-based, clamped) the current lesson and saves it.
    void SetCurrent(int lesson);
    void MarkDone(int lesson);
    bool IsDone(int lesson) const;
    int DoneCount() const;

    // Screen text of a lesson: words, phrases and grammar.
    std::string ScreenText(int lesson) const;
    // Header line, e.g. "Bài 3/36 · Ça va bien ?".
    std::string ScreenTitle(int lesson) const;

    // A word from the lessons learnt so far (up to the current one), missed words first.
    Quiz PickQuiz();
    void RecordQuiz(int lesson, int word, bool correct);
    int WeakCount() const;

    // Shows the current lesson (or the next one) on screen, if a viewer is set.
    void ShowLesson(int lesson);
    void ShowQuiz();

private:
    FrenchCourse();
    void Load();
    void Save() const;
    std::string LessonForModel(int lesson) const;
    std::string ProgressForModel() const;

    int current_ = 0;
    uint64_t done_ = 0;
    // Missed words as (lesson << 8 | word); oldest first, at most kMaxWeak.
    static constexpr int kMaxWeak = 40;
    uint16_t weak_[kMaxWeak] = {};
    int weak_count_ = 0;
    uint32_t quiz_ok_ = 0;
    uint32_t quiz_total_ = 0;
    uint32_t pick_counter_ = 0;
    LessonViewer lesson_viewer_;
    QuizViewer quiz_viewer_;
};

#endif  // FRENCH_COURSE_H
