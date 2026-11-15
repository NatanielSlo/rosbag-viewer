#pragma once
// The single clock of a replay: play, pause, seek, speed. It is advanced by the caller with the
// wall-clock time that passed (e.g. from a QTimer), so it is deterministic and easy to test.
#include <algorithm>

namespace viewer {

class ReplayClock {
public:
    // Time is in seconds since the start of the recording.
    void setRange(double start, double end) {
        start_ = start;
        end_ = std::max(start, end);
        position_ = std::clamp(position_, start_, end_);
    }
    double start() const { return start_; }
    double end() const { return end_; }
    double duration() const { return end_ - start_; }

    double position() const { return position_; }
    void seek(double t) { position_ = std::clamp(t, start_, end_); }

    bool playing() const { return playing_; }
    // Playing from the end starts again from the beginning.
    void play() {
        if (speed_ > 0 && position_ >= end_) position_ = start_;
        if (speed_ < 0 && position_ <= start_) position_ = end_;
        playing_ = true;
    }
    void pause() { playing_ = false; }
    void toggle() { playing_ ? pause() : play(); }

    double speed() const { return speed_; }
    void setSpeed(double s) { speed_ = s; }

    bool loop() const { return loop_; }
    void setLoop(bool l) { loop_ = l; }

    // Moves the clock by `wallSeconds` of real time. Returns true while still playing.
    bool advance(double wallSeconds) {
        if (!playing_) return false;
        position_ += wallSeconds * speed_;
        if (position_ >= end_ || position_ <= start_) {
            if (loop_ && duration() > 0) {
                position_ =
                    position_ >= end_ ? start_ + (position_ - end_) : end_ - (start_ - position_);
                position_ = std::clamp(position_, start_, end_);
            } else {
                position_ = std::clamp(position_, start_, end_);
                playing_ = false;  // reached an end
            }
        }
        return playing_;
    }

private:
    double start_ = 0, end_ = 0, position_ = 0, speed_ = 1.0;
    bool playing_ = false, loop_ = false;
};

}  // namespace viewer
