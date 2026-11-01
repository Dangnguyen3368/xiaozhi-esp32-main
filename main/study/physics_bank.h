#ifndef PHYSICS_BANK_H
#define PHYSICS_BANK_H

#include <cstddef>
#include <cstdint>

// Topics of the grade-10 specialized physics entrance exam (Lam Son, Thanh Hoa).
enum PhysicsTopic : uint8_t {
    kTopicMechanics = 0,  // co
    kTopicHeat,           // nhiet
    kTopicElectricity,    // dien
    kTopicOptics,         // quang
    kTopicPractice,       // thuchanh
    kTopicCount,
};

// The screen image of a problem: its line drawing, if any, with a summary of the given data
// underneath. 1 bit per pixel, rows of (width + 7) / 8 bytes, most significant bit first,
// 1 = ink. Shown on colour screens; voice-only boards ignore it.
struct PhysicsFigure {
    uint16_t width;
    uint16_t height;
    const uint8_t* bits;
    bool has_drawing;  // false when the image is only the data summary
};

struct PhysicsProblem {
    const char* id;
    uint8_t topic;
    uint8_t level;  // 1 basic, 2 good, 3 specialized-exam
    const char* title;
    const char* question;
    const char* answer;
    const char* hints[3];
    uint8_t hint_count;
    const char* solution;
    const PhysicsFigure* figure;  // screen image, nullptr when there is none
};

extern const PhysicsProblem kPhysicsProblems[];
extern const size_t kPhysicsProblemCount;

#endif  // PHYSICS_BANK_H
