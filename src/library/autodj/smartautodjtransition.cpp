#include "library/autodj/smartautodjtransition.h"
#include "track/track.h"


#include <algorithm>
#include <cmath>

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

double SmartAutoDJTransition::calculateCrossfaderPosition(
        double progress,
        bool fromLeftToRight,
        CrossfadeCurve curve) {
    double p = std::clamp(progress, 0.0, 1.0);
    double shapedProgress = p;

    switch (curve) {
    case CrossfadeCurve::EqualPower:
        // Equal power curve using sine/cosine energy preservation
        shapedProgress = std::sin(p * M_PI_2) * std::sin(p * M_PI_2);
        break;
    case CrossfadeCurve::Smoothstep:
        // S-curve interpolation (smooth start and smooth landing)
        shapedProgress = p * p * (3.0 - (2.0 * p));
        break;
    case CrossfadeCurve::FastCut:
        // Exponential fast cut
        shapedProgress = std::sqrt(p);
        break;
    case CrossfadeCurve::Linear:
    default:
        shapedProgress = p;
        break;
    }

    if (fromLeftToRight) {
        return -1.0 + (2.0 * shapedProgress);
    } else {
        return 1.0 - (2.0 * shapedProgress);
    }
}

SmartAutoDJTransition::TransitionState SmartAutoDJTransition::calculateTransitionState(
        double progress,
        bool fromLeftToRight,
        CrossfadeCurve curve,
        bool swapBassAtMidpoint) {
    TransitionState state;
    double p = std::clamp(progress, 0.0, 1.0);

    state.crossfaderPosition = calculateCrossfaderPosition(p, fromLeftToRight, curve);

    if (swapBassAtMidpoint) {
        // Intelligent Low-EQ (Bass) Frequency Swapping
        // Prevents low-frequency phase cancellation and bass mud
        if (p < 0.40) {
            state.fromDeckEQ.low = 1.0;
            state.toDeckEQ.low = 0.0; // Duck incoming bass during buildup
        } else if (p >= 0.40 && p < 0.50) {
            // Smoothly cut outgoing bass right before the phrase drop
            double cutFactor = (0.50 - p) / 0.10;
            state.fromDeckEQ.low = std::clamp(cutFactor, 0.0, 1.0);
            state.toDeckEQ.low = 0.0;
        } else if (p >= 0.50 && p < 0.60) {
            // Drop! Incoming bass drops in at full force
            double inFactor = (p - 0.50) / 0.10;
            state.fromDeckEQ.low = 0.0;
            state.toDeckEQ.low = std::clamp(inFactor, 0.0, 1.0);
        } else {
            state.fromDeckEQ.low = 0.0;
            state.toDeckEQ.low = 1.0;
        }

        // Smooth high/mid frequency cross-fade
        state.fromDeckEQ.mid = std::clamp(1.0 - (0.4 * p), 0.0, 1.0);
        state.fromDeckEQ.high = std::clamp(1.0 - (0.5 * p), 0.0, 1.0);

        state.toDeckEQ.mid = std::clamp(0.6 + (0.4 * p), 0.0, 1.0);
        state.toDeckEQ.high = std::clamp(0.5 + (0.5 * p), 0.0, 1.0);
    } else {
        // Standard non-EQ transition (unity EQ)
        state.fromDeckEQ = {1.0, 1.0, 1.0};
        state.toDeckEQ = {1.0, 1.0, 1.0};
    }

    return state;
}

double SmartAutoDJTransition::findNearestPhraseBoundary(
        const mixxx::BeatsPointer& pBeats,
        double positionSeconds,
        int barsPerPhrase,
        double fallbackBpm) {
    if (positionSeconds <= 0.0) {
        return 0.0;
    }

    int bars = (barsPerPhrase > 0) ? barsPerPhrase : 16;
    double bpm = (fallbackBpm > 20.0) ? fallbackBpm : 120.0;

    if (pBeats) {
        mixxx::audio::FramePos currentFrame =
                mixxx::audio::FramePos::fromEngineSamplePosMaybeInvalid(
                        positionSeconds * 44100.0 * 2.0); // nominal frame estimation
        auto prevBeat = pBeats->findPrevBeat(currentFrame);
        auto nextBeat = pBeats->findNextBeat(currentFrame);

        if (prevBeat.isValid() && nextBeat.isValid()) {
            double beatDuration = (nextBeat - prevBeat) / 44100.0;
            if (beatDuration > 0.1 && beatDuration < 2.0) {
                bpm = 60.0 / beatDuration;
            }
        }
    }

    double secondsPerBeat = 60.0 / bpm;
    double secondsPerPhrase = secondsPerBeat * 4.0 * static_cast<double>(bars);

    double phraseIndex = std::round(positionSeconds / secondsPerPhrase);
    return std::max(0.0, phraseIndex * secondsPerPhrase);
}

double SmartAutoDJTransition::detectRealMusicStartSecond(
        const TrackPointer& pTrack,
        double sampleRate) {
    if (!pTrack) {
        return 0.0;
    }

    // 1. Check if there is an explicit Intro End marker.
    // If the user has set the Intro End, use it as the definitive music start.
    CuePointer pIntroCue = pTrack->findCueByType(mixxx::CueType::Intro);
    if (pIntroCue && pIntroCue->getEndPosition().isValid()) {
        double effectiveSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
        return pIntroCue->getEndPosition().value() / (effectiveSampleRate * 2.0);
    }

    // 2. Retrieve Waveform Data
    ConstWaveformPointer pWaveform = pTrack->getWaveformSummary();
    if (!pWaveform) {
        pWaveform = pTrack->getWaveform();
    }

    if (!pWaveform || pWaveform->getDataSize() <= 0) {
        return 0.0;
    }

    int dataSize = pWaveform->getDataSize();
    double ratio = pWaveform->getAudioVisualRatio();
    if (ratio <= 0.0) {
        ratio = 1024.0;
    }
    double effectiveSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;

    // Helper to convert time in seconds to visual sample index
    auto timeToIndex = [ratio, effectiveSampleRate](double sec) {
        return static_cast<int>((sec * effectiveSampleRate * 2.0) / ratio);
    };

    // Calculate maximum intensity across the entire track
    unsigned char maxLow = 0;
    unsigned char maxAll = 0;
    for (int i = 0; i < dataSize; ++i) {
        maxLow = std::max(maxLow, pWaveform->getLow(i));
        maxAll = std::max(maxAll, pWaveform->getAll(i));
    }

    if (maxAll == 0) {
        return 0.0;
    }

    // Scan the first 45 seconds of the track
    int scanLimitIndex = std::min(timeToIndex(45.0), dataSize);
    int windowSize = std::max(1, timeToIndex(1.5)); // 1.5 seconds window

    double bestStartSecond = 0.0;
    double maxRatioIncrease = 0.0;

    // Look for a step-increase (buildup / drop transition)
    for (int i = windowSize; i < scanLimitIndex - windowSize; ++i) {
        // Calculate average of previous window
        double prevSum = 0.0;
        for (int w = -windowSize; w < 0; ++w) {
            prevSum += pWaveform->getAll(i + w);
        }
        double prevAvg = prevSum / windowSize;

        // Calculate average of next window
        double nextSum = 0.0;
        for (int w = 0; w < windowSize; ++w) {
            nextSum += pWaveform->getAll(i + w);
        }
        double nextAvg = nextSum / windowSize;

        // We look for a significant volume jump:
        // 1. Next average must be at least 2.5x higher than previous average
        // 2. Next average must be at least 20% of the overall maximum intensity of the track
        if (prevAvg > 1.0) {
            double ratioIncrease = nextAvg / prevAvg;
            if (ratioIncrease > maxRatioIncrease && ratioIncrease >= 2.5 && nextAvg >= (maxAll * 0.40)) {
                maxRatioIncrease = ratioIncrease;
                bestStartSecond = (i * ratio) / (effectiveSampleRate * 2.0);
            }
        }
    }

    if (bestStartSecond > 0.0) {
        return bestStartSecond;
    }

    // Fallback: If no sudden step increase, look for the first point where volume
    // climbs above 40% of maximum intensity.
    double absoluteThreshold = maxAll * 0.40;
    for (int i = 0; i < scanLimitIndex; ++i) {
        if (pWaveform->getAll(i) >= absoluteThreshold) {
            return (i * ratio) / (effectiveSampleRate * 2.0);
        }
    }

    return 0.0;
}
