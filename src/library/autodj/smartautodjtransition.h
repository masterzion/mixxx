#pragma once

#include <cmath>
#include <QPair>

#include "audio/frame.h"
#include "track/beats.h"
#include "track/track_decl.h"

class SmartAutoDJTransition {
  public:
    enum class CrossfadeCurve {
        Linear = 0,
        EqualPower = 1,
        Smoothstep = 2,
        FastCut = 3
    };

    struct EQLevels {
        double low = 1.0;   // 0.0 (kill) to 1.0 (unity) to 4.0 (boost)
        double mid = 1.0;
        double high = 1.0;
    };

    struct TransitionState {
        double crossfaderPosition = 0.0; // -1.0 (Left) to +1.0 (Right)
        EQLevels fromDeckEQ;
        EQLevels toDeckEQ;
    };

    /// Calculate crossfader position (-1.0 to 1.0) from linear transition progress (0.0 to 1.0).
    static double calculateCrossfaderPosition(
            double progress,
            bool fromLeftToRight,
            CrossfadeCurve curve = CrossfadeCurve::EqualPower);

    /// Calculate both crossfader and automated EQ Low/Mid/High levels during the transition.
    /// When swapBassAtMidpoint is true, cleanly ducks and transitions the low frequencies
    /// at the 50% phrase drop to prevent low-end phase clashing.
    static TransitionState calculateTransitionState(
            double progress,
            bool fromLeftToRight,
            CrossfadeCurve curve = CrossfadeCurve::EqualPower,
            bool swapBassAtMidpoint = true);

    /// Finds the nearest musical phrase boundary (e.g. 16 or 32 bars) around a given time position.
    static double findNearestPhraseBoundary(
            const mixxx::BeatsPointer& pBeats,
            double positionSeconds,
            int barsPerPhrase = 16,
            double fallbackBpm = 120.0);

    /// Detects the real start of the music (skipping silence and long ambient intros)
    /// based on intro cue points, beatgrids, and waveform summaries.
    static double detectRealMusicStartSecond(
            const TrackPointer& pTrack,
            double sampleRate = 44100.0);
};
