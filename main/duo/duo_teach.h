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

// Sent to the server as the detected wake word when the partner robot hands over the turn.
// xiaozhi.me accepts only real wake words in "listen detect" (any other text is rejected with
// "Detect is only for wake words"), so the cue is the robot's own wake word and the assistant
// fetches the partner's words through self.duo.mode action partner_said.
#define DUO_CUE "你好小智"

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
    void AddSpokenText(const std::string& text);  // One sentence of this robot's reply
    void OnReplyRequested();  // Sent the partner's speech to the server; waiting for the answer
    void OnHeard();           // The server recognised speech (STT); the answer is on its way

private:
    DuoTeach() = default;

    struct Outgoing {
        std::string payload;
        int copies_left;
    };

    void Task();
    void Broadcast(const std::string& event, int turn = 0, bool final_turn = false,
                   const std::string& text = "");
    void HandleMessage(const char* data, int len, const struct sockaddr_in& from);
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
    uint32_t my_boot_ = 0;          // Random per start, so the partner notices a restart
    uint32_t peer_boot_ = 0;
    std::vector<uint32_t> peer_seen_seqs_;  // Recent message numbers, to drop repeated copies
    int64_t peer_seen_us_ = 0;
    bool peer_speaking_ = false;
    int64_t peer_quiet_since_us_ = 0;
    int peer_turn_ = 0;
    uint32_t peer_ip_ = 0;          // Network byte order; 0 until the partner is heard
    std::string peer_text_;         // What the partner said in its last turn (from its TTS text)

    // This robot's state.
    bool speaking_ = false;
    int my_turn_ = 0;
    bool end_pending_ = false;      // Waiting for playback to drain before announcing the end
    bool end_final_ = false;
    int64_t end_wait_since_us_ = 0;
    int64_t claim_us_ = 0;
    bool peer_listening_ = false;   // Recording the partner right now
    std::string spoken_text_;       // This robot's current reply, sent with "end"
    // Waiting for the server to answer the partner: 1 = after the recording, 2 = recognised,
    // 3 = after sending the partner's words as text. Each stage falls back to the next.
    int reply_stage_ = 0;
    bool partner_turn_pending_ = false;  // The wake cue was for the partner's words, not a student
    int64_t reply_stage_since_us_ = 0;
    int64_t peer_listen_since_us_ = 0;
};

#endif  // DUO_TEACH_H
