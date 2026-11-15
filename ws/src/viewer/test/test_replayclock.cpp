#include <gtest/gtest.h>

#include "model/replayclock.h"

using viewer::ReplayClock;

namespace {
ReplayClock make() {
    ReplayClock c;
    c.setRange(0, 100);
    return c;
}
}  // namespace

TEST(ReplayClock, DoesNotMoveWhilePaused) {
    auto c = make();
    EXPECT_FALSE(c.advance(5));
    EXPECT_DOUBLE_EQ(c.position(), 0);
}

TEST(ReplayClock, AdvancesWithSpeed) {
    auto c = make();
    c.play();
    c.setSpeed(4);
    EXPECT_TRUE(c.advance(2.5));
    EXPECT_DOUBLE_EQ(c.position(), 10);
}

TEST(ReplayClock, StopsAtTheEnd) {
    auto c = make();
    c.seek(98);
    c.play();
    EXPECT_FALSE(c.advance(5));
    EXPECT_DOUBLE_EQ(c.position(), 100);
    EXPECT_FALSE(c.playing());
}

TEST(ReplayClock, PlayFromTheEndRestarts) {
    auto c = make();
    c.seek(100);
    c.play();
    EXPECT_DOUBLE_EQ(c.position(), 0);
    EXPECT_TRUE(c.playing());
}

TEST(ReplayClock, LoopWrapsAround) {
    auto c = make();
    c.setLoop(true);
    c.seek(98);
    c.play();
    EXPECT_TRUE(c.advance(5));
    EXPECT_NEAR(c.position(), 3, 1e-9);
}

TEST(ReplayClock, SeekIsClamped) {
    auto c = make();
    c.seek(-5);
    EXPECT_DOUBLE_EQ(c.position(), 0);
    c.seek(500);
    EXPECT_DOUBLE_EQ(c.position(), 100);
}

TEST(ReplayClock, ReverseStopsAtTheStart) {
    auto c = make();
    c.seek(3);
    c.setSpeed(-1);
    c.play();
    EXPECT_FALSE(c.advance(10));
    EXPECT_DOUBLE_EQ(c.position(), 0);
}

TEST(ReplayClock, ShrinkingTheRangeKeepsPositionInside) {
    auto c = make();
    c.seek(80);
    c.setRange(0, 50);
    EXPECT_DOUBLE_EQ(c.position(), 50);
}
