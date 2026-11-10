#ifndef DUO_TEACH_H
#define DUO_TEACH_H

// Two robots teaching together (chế độ hai robot cùng giảng bài).
//
// Both robots stay on the same Wi-Fi and tell each other over UDP broadcast when they start and
// finish speaking. While one robot speaks, the other records it in manual listening mode (so the
// server does not cut it off at a pause and it never talks over its partner). When the speaker
// finishes, the listener sends "stop listening" and the server answers what the partner said.
// After `turns` replies in a row between the robots, the last speaker opens its microphone for
// the student again. A robot woken by the student (wake word, button, touch) takes the floor and
// its partner goes quiet.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class DuoTeach {
public:
    static DuoTeach& GetInstance();

    // Loads the settings, registers the self.duo.mode tool and starts the UDP task.
    void Start();

    bool enabled() const { return enabled_.load(); }
    bool PeerOnline() const;

    // What the robot should do when its reply has finished.
    enum class AfterSpeech { kNormal, kHandOff, kListenStudent };

    // Called from the main task by Application.
    void OnSpeakStart(bool reply_to_peer);
    AfterSpeech OnSpeakStop();
    void OnClaimFloor();      // The student woke this robot.
    void CancelSpeech();      // This reply was cut short; do not hand the turn on.
    void OnPeerListenStart();
    void OnPeerListenEnd();

private:
    DuoTeach() = default;

    struct Outgoing {
        std::string payload;
        int copies_left;
    };

    void Task();
    void Broadcast(const std::string& event, int turn = 0, bool final_turn = false);
    void HandleMessage(const char* data, int len);
    void SaveSettings();
    std::string StatusText() const;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> started_{false};
    int max_turns_ = 4;
    std::string my_id_;
    uint32_t seq_ = 0;

    mutable std::mutex mutex_;
    std::vector<Outgoing> outbox_;
    std::string peer_id_;
    uint32_t peer_last_seq_ = 0;
    int64_t peer_seen_us_ = 0;
    bool peer_speaking_ = false;
    int64_t peer_quiet_since_us_ = 0;
    int peer_turn_ = 0;

    // This robot's state.
    bool speaking_ = false;
    int my_turn_ = 0;
    bool end_pending_ = false;      // Waiting for playback to drain before announcing the end
    bool end_final_ = false;
    int64_t end_wait_since_us_ = 0;
    int64_t claim_us_ = 0;
    bool peer_listening_ = false;   // Recording the partner right now
    int64_t peer_listen_since_us_ = 0;
};

#endif  // DUO_TEACH_H
