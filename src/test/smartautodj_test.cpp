#include <gtest/gtest.h>

#include "library/autodj/smartautodjmatcher.h"
#include "library/autodj/smartautodjtransition.h"
#include "proto/keys.pb.h"

namespace {

using mixxx::track::io::key::ChromaticKey;

TEST(SmartAutoDJTest, HarmonicKeyMatching) {
    // Exact Key Match
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::A_MINOR, ChromaticKey::A_MINOR)); // 8A -> 8A

    // Adjacent Camelot Key (+1 / -1 Energy shift)
    EXPECT_DOUBLE_EQ(0.90, SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::A_MINOR, ChromaticKey::E_MINOR)); // 8A -> 9A
    EXPECT_DOUBLE_EQ(0.90, SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::A_MINOR, ChromaticKey::D_MINOR)); // 8A -> 7A

    // Relative Major / Minor (8A <-> 8B)
    EXPECT_DOUBLE_EQ(0.85, SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::A_MINOR, ChromaticKey::C_MAJOR)); // 8A -> 8B

    // Energy Boost (+2 Camelot steps)
    EXPECT_DOUBLE_EQ(0.75, SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::A_MINOR, ChromaticKey::B_MINOR)); // 8A -> 10A

    // Incompatible / Dissonant Keys
    EXPECT_LE(SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::A_MINOR, ChromaticKey::F_SHARP_MAJOR), 0.30);

    // Invalid Key returns neutral 0.5
    EXPECT_DOUBLE_EQ(0.50, SmartAutoDJMatcher::calculateHarmonicScore(
            ChromaticKey::INVALID, ChromaticKey::A_MINOR));
}

TEST(SmartAutoDJTest, BpmScoreCalculation) {
    // Exact BPM Match
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateBpmScore(128.0, 128.0));

    // Within 2% tempo difference
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateBpmScore(128.0, 129.5));

    // Moderate tempo difference (~4%)
    double score4Percent = SmartAutoDJMatcher::calculateBpmScore(125.0, 130.0, 8.0);
    EXPECT_GT(score4Percent, 0.65);
    EXPECT_LT(score4Percent, 1.0);

    // Half-time / Double-time matching (e.g. 70 BPM <-> 140 BPM, 174 BPM <-> 87 BPM)
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateBpmScore(140.0, 70.0, 8.0, true));
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateBpmScore(87.0, 174.0, 8.0, true));

    // Incompatible BPM beyond threshold (e.g. 90 BPM vs 140 BPM)
    EXPECT_LT(SmartAutoDJMatcher::calculateBpmScore(90.0, 140.0, 8.0, false), 0.20);
}

TEST(SmartAutoDJTest, GenreScoreCalculation) {
    // Exact match
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateGenreScore("House", "House"));
    EXPECT_DOUBLE_EQ(1.0, SmartAutoDJMatcher::calculateGenreScore("deep house", "Deep House"));

    // Overlapping compound genre tags
    double overlapScore = SmartAutoDJMatcher::calculateGenreScore(
            "Tech House / Minimal", "Deep House / Tech House");
    EXPECT_GE(overlapScore, 0.70);

    // Unrelated genres
    double disparateScore = SmartAutoDJMatcher::calculateGenreScore("Classical", "Drum & Bass");
    EXPECT_LE(disparateScore, 0.30);
}

TEST(SmartAutoDJTest, CrossfadeCurves) {
    // Test EqualPower Curve endpoints and midpoint
    double leftStart = SmartAutoDJTransition::calculateCrossfaderPosition(
            0.0, true, SmartAutoDJTransition::CrossfadeCurve::EqualPower);
    EXPECT_NEAR(-1.0, leftStart, 1e-4);

    double centerMid = SmartAutoDJTransition::calculateCrossfaderPosition(
            0.5, true, SmartAutoDJTransition::CrossfadeCurve::EqualPower);
    EXPECT_NEAR(0.0, centerMid, 1e-4);

    double rightEnd = SmartAutoDJTransition::calculateCrossfaderPosition(
            1.0, true, SmartAutoDJTransition::CrossfadeCurve::EqualPower);
    EXPECT_NEAR(1.0, rightEnd, 1e-4);

    // Reverse direction (Right to Left)
    double rStart = SmartAutoDJTransition::calculateCrossfaderPosition(
            0.0, false, SmartAutoDJTransition::CrossfadeCurve::EqualPower);
    EXPECT_NEAR(1.0, rStart, 1e-4);

    double rEnd = SmartAutoDJTransition::calculateCrossfaderPosition(
            1.0, false, SmartAutoDJTransition::CrossfadeCurve::EqualPower);
    EXPECT_NEAR(-1.0, rEnd, 1e-4);
}

TEST(SmartAutoDJTest, EQBassSwapping) {
    // Early transition phase (progress = 0.20): Outgoing bass is full, Incoming bass is ducked
    auto stateEarly = SmartAutoDJTransition::calculateTransitionState(
            0.20, true, SmartAutoDJTransition::CrossfadeCurve::EqualPower, true);
    EXPECT_DOUBLE_EQ(1.0, stateEarly.fromDeckEQ.low);
    EXPECT_DOUBLE_EQ(0.0, stateEarly.toDeckEQ.low);

    // After phrase drop (progress = 0.70): Outgoing bass is cut, Incoming bass is full
    auto stateLate = SmartAutoDJTransition::calculateTransitionState(
            0.70, true, SmartAutoDJTransition::CrossfadeCurve::EqualPower, true);
    EXPECT_DOUBLE_EQ(0.0, stateLate.fromDeckEQ.low);
    EXPECT_DOUBLE_EQ(1.0, stateLate.toDeckEQ.low);
}

TEST(SmartAutoDJTest, PhraseAlignment) {
    // At 120 BPM: 0.5s per beat, 2.0s per 4-beat bar.
    // 16-bar phrase = 32.0 seconds.
    // Position 30.0s should snap to nearest 16-bar boundary at 32.0s.
    double aligned = SmartAutoDJTransition::findNearestPhraseBoundary(
            nullptr, 30.0, 16, 120.0);
    EXPECT_NEAR(32.0, aligned, 1e-4);

    // Position 10.0s should snap to 0.0s (or 32.0s depending on proximity)
    double alignedZero = SmartAutoDJTransition::findNearestPhraseBoundary(
            nullptr, 10.0, 16, 120.0);
    EXPECT_NEAR(0.0, alignedZero, 1e-4);
}

} // namespace
