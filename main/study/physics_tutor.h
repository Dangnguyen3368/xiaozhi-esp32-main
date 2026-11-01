#ifndef PHYSICS_TUTOR_H
#define PHYSICS_TUTOR_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct PhysicsFigure;

// Exam practice for the grade-10 specialized physics exam: serves problems from the
// built-in bank, keeps progress per learner in NVS, and gives the model the stored
// answer and solution so it can grade and coach the student.
class PhysicsTutor {
public:
    static PhysicsTutor& GetInstance();

    // Registers the self.study.* MCP tools. Call once from the board's InitializeTools().
    void RegisterTools();

    // Boards with a colour screen show problem figures through this callback. It is called
    // with the problem's figure (nullptr to hide it) and a short caption.
    using FigureViewer = std::function<void(const PhysicsFigure* figure, const std::string& caption)>;
    void SetFigureViewer(FigureViewer viewer);

private:
    struct Learner {
        std::string name;
        std::vector<uint8_t> done;   // one bit per problem, indexed by bank position
        std::vector<uint8_t> wrong;  // set while the last attempt was wrong
    };

    PhysicsTutor() = default;

    void Load();
    void SaveNames();
    void SaveLearner(size_t index);
    Learner& Current();
    int FindLearner(const std::string& name) const;
    Learner MakeLearner(const std::string& name) const;

    std::vector<size_t> Candidates(int topic, int level, bool only_new, bool only_wrong);
    int FindProblem(const std::string& id) const;
    void ShowOnScreen(size_t index);

    std::vector<Learner> learners_;
    FigureViewer figure_viewer_;
    size_t current_ = 0;
    bool loaded_ = false;
};

#endif  // PHYSICS_TUTOR_H
