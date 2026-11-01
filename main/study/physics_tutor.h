#ifndef PHYSICS_TUTOR_H
#define PHYSICS_TUTOR_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <esp_timer.h>

struct PhysicsFigure;

// Exam practice for the grade-10 specialized physics exam: serves problems from the
// built-in bank, keeps progress per learner in NVS, and gives the model the stored
// answer and solution so it can grade and coach the student.
class PhysicsTutor {
public:
    static PhysicsTutor& GetInstance();

    // Registers the self.study.* MCP tools and starts the daily review check. Call once from
    // the board's InitializeTools().
    void RegisterTools();

    // What a colour screen shows: a problem (with its quick-action buttons) or a formula sheet.
    enum class ScreenKind { kProblem, kFormulas };

    // Boards with a colour screen show problem figures through this callback. It is called
    // with the image (nullptr to hide it), a short caption and what the image is.
    using FigureViewer =
        std::function<void(const PhysicsFigure* figure, const std::string& caption, ScreenKind kind)>;
    void SetFigureViewer(FigureViewer viewer);

    struct TopicProgress {
        const char* name;
        int done;
        int total;
        int need_review;
    };
    struct ProgressReport {
        std::string learner;
        std::vector<TopicProgress> topics;
        int done = 0;
        int total = 0;
        int need_review = 0;
        int due_today = 0;
    };
    // Boards with a colour screen draw the progress chart through this callback.
    using ProgressViewer = std::function<void(const ProgressReport& report)>;
    void SetProgressViewer(ProgressViewer viewer);

    // Progress of the active learner; also draws it when a progress viewer is set.
    ProgressReport ShowProgress();
    // Shows the formula sheet of a topic key (co, nhiet, dien, quang); false if unknown.
    bool ShowFormulas(const std::string& topic);

    // For the quick buttons under the problem on screen: the next hint (cycling back to the
    // first after the last) or the answer with the full solution. False when no problem was
    // shown yet.
    bool NextHintOnScreen(std::string& title, std::string& text);
    bool SolutionOnScreen(std::string& title, std::string& text);
    // Picks a problem for the current learner like get_problem (new or spaced review) and shows
    // it on screen without the server. Returns its id, or "" when there is none.
    std::string PickOnScreen(bool review);

    // A wrong problem waiting for spaced review: due again on `due_day` (days since the
    // epoch, local time). Each correct review moves it up a box and spaces it further apart;
    // after the last box it leaves the review list.
    struct ReviewItem {
        uint16_t index;
        uint8_t box;
        int32_t due_day;
    };

private:
    struct Learner {
        std::string name;
        std::vector<uint8_t> done;   // one bit per problem, indexed by bank position
        std::vector<uint8_t> wrong;  // set while the problem is in the review list
        std::vector<ReviewItem> review;
    };

    PhysicsTutor() = default;

    void Load();
    void SaveNames();
    void SaveLearner(size_t index);
    Learner& Current();
    int FindLearner(const std::string& name) const;
    Learner MakeLearner(const std::string& name) const;

    std::vector<size_t> Candidates(int topic, int level, bool only_new, bool only_wrong);
    std::vector<size_t> DueReviews(const Learner& learner, int topic) const;
    void RecordResult(Learner& learner, size_t index, bool correct);
    int FindProblem(const std::string& id) const;
    int PickProblem(const std::string& mode, int topic, int level, const char*& note, std::string& error);
    void ShowOnScreen(size_t index);

    void StartDailyReviewTimer();
    void CheckDailyReview();

    std::vector<Learner> learners_;
    FigureViewer figure_viewer_;
    ProgressViewer progress_viewer_;
    size_t current_ = 0;
    int screen_problem_ = -1;  // bank index of the problem on screen
    int screen_hints_ = 0;     // hints already shown for it with the hint button
    bool loaded_ = false;

    // Daily review reminder: minutes after local midnight, or -1 when off.
    int review_minute_ = -1;
    int32_t review_fired_day_ = -1;
    esp_timer_handle_t review_timer_ = nullptr;
};

#endif  // PHYSICS_TUTOR_H
