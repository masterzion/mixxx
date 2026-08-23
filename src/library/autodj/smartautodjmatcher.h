#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "proto/keys.pb.h"
#include "track/track_decl.h"
#include "track/keyutils.h"

class SmartAutoDJMatcher {
  public:
    struct MatchScores {
        double keyScore = 0.0;     // 0.0 to 1.0 (Harmonic compatibility)
        double bpmScore = 0.0;     // 0.0 to 1.0 (BPM / tempo proximity)
        double genreScore = 0.0;   // 0.0 to 1.0 (Genre matching)
        double energyScore = 0.0;  // 0.0 to 1.0 (Energy level alignment)
        double totalScore = 0.0;   // Weighted composite score (0.0 to 1.0)
        QString explanation;       // Human-readable summary of match reasons
    };

    struct MatchWeights {
        double keyWeight = 0.35;
        double bpmWeight = 0.35;
        double genreWeight = 0.20;
        double energyWeight = 0.10;
        double maxBpmDeltaPercent = 8.0;
        bool allowHalfDoubleBpm = true;
    };

    /// Calculate harmonic compatibility score based on the Camelot Circle of Fifths.
    /// Returns 1.0 for same key, 0.9 for adjacent (energy shift), 0.85 for relative major/minor,
    /// 0.75 for energy boost (+2), down to 0.1 for dissonant keys.
    static double calculateHarmonicScore(
            mixxx::track::io::key::ChromaticKey currentKey,
            mixxx::track::io::key::ChromaticKey candidateKey);

    /// Calculate BPM proximity score.
    /// Returns 1.0 for exact/near match (within 2%), smoothly dropping to 0.0 at maxBpmDeltaPercent.
    /// If allowHalfDoubleBpm is true, checks 0.5x and 2.0x tempos as well.
    static double calculateBpmScore(
            double currentBpm,
            double candidateBpm,
            double maxBpmDeltaPercent = 8.0,
            bool allowHalfDoubleBpm = true);

    /// Calculate Genre similarity score between two genre strings.
    /// Handles multiple tags (e.g. "Deep House / Progressive", "Tech-House").
    static double calculateGenreScore(
            const QString& currentGenre,
            const QString& candidateGenre);

    /// Calculate Energy similarity score based on ReplayGain and BPM density.
    static double calculateEnergyScore(
            const TrackPointer& pCurrentTrack,
            const TrackPointer& pCandidateTrack);

    /// Comprehensive evaluation of a candidate track against the current track.
    static MatchScores evaluateCandidate(
            const TrackPointer& pCurrentTrack,
            const TrackPointer& pCandidateTrack,
            const MatchWeights& weights = MatchWeights());

    /// Rank a list of candidate tracks by composite compatibility score.
    static QList<QPair<TrackPointer, MatchScores>> rankCandidates(
            const TrackPointer& pCurrentTrack,
            const QList<TrackPointer>& candidates,
            const MatchWeights& weights = MatchWeights());
};
