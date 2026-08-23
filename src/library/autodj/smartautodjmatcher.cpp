#include "library/autodj/smartautodjmatcher.h"

#include <algorithm>
#include <cmath>
#include <QSet>

#include "track/track.h"
#include "util/math.h"

double SmartAutoDJMatcher::calculateHarmonicScore(
        mixxx::track::io::key::ChromaticKey currentKey,
        mixxx::track::io::key::ChromaticKey candidateKey) {
    if (currentKey == mixxx::track::io::key::INVALID ||
            candidateKey == mixxx::track::io::key::INVALID) {
        return 0.5; // Neutral score when key metadata is unknown
    }

    if (currentKey == candidateKey) {
        return 1.0; // Perfect match
    }

    int currentNum = KeyUtils::keyToOpenKeyNumber(currentKey);
    int candidateNum = KeyUtils::keyToOpenKeyNumber(candidateKey);
    bool currentMajor = KeyUtils::keyIsMajor(currentKey);
    bool candidateMajor = KeyUtils::keyIsMajor(candidateKey);

    int diff = std::abs(currentNum - candidateNum);
    int clockDist = std::min(diff, 12 - diff);

    if (currentMajor == candidateMajor) {
        if (clockDist == 1) {
            return 0.90; // Adjacent Camelot key (Energy Shift +1 / -1)
        }
        if (clockDist == 2) {
            return 0.75; // Energy Boost (+2 on Circle of Fifths)
        }
        if (clockDist == 3) {
            return 0.40;
        }
        if (clockDist == 4) {
            return 0.30;
        }
        return 0.15; // Incompatible harmonic key
    } else {
        if (clockDist == 0) {
            return 0.85; // Relative Major / Minor (e.g. 8A <-> 8B)
        }
        if (clockDist == 1) {
            return 0.70; // Diagonal Camelot step
        }
        return 0.20;
    }
}

double SmartAutoDJMatcher::calculateBpmScore(
        double currentBpm,
        double candidateBpm,
        double maxBpmDeltaPercent,
        bool allowHalfDoubleBpm) {
    if (currentBpm <= 0.0 || candidateBpm <= 0.0) {
        return 0.5; // Neutral score if BPM is unanalyzed
    }

    double deltaDirect = std::fabs(candidateBpm - currentBpm) / currentBpm * 100.0;
    double bestDelta = deltaDirect;

    if (allowHalfDoubleBpm) {
        double deltaDouble = std::fabs((candidateBpm * 0.5) - currentBpm) / currentBpm * 100.0;
        double deltaHalf = std::fabs((candidateBpm * 2.0) - currentBpm) / currentBpm * 100.0;
        bestDelta = std::min({deltaDirect, deltaDouble, deltaHalf});
    }

    if (bestDelta <= 2.0) {
        return 1.0; // Within 2% tempo variance is practically transparent
    }

    if (bestDelta <= maxBpmDeltaPercent) {
        // Linear decay from 1.0 down to 0.25 at the tolerance threshold
        double factor = (bestDelta - 2.0) / std::max(1.0, maxBpmDeltaPercent - 2.0);
        return 1.0 - (0.75 * factor);
    }

    // Beyond threshold, rapidly drop towards 0.0
    double overflow = bestDelta - maxBpmDeltaPercent;
    return std::max(0.0, 0.25 - (0.05 * overflow));
}

double SmartAutoDJMatcher::calculateGenreScore(
        const QString& currentGenre,
        const QString& candidateGenre) {
    QString g1 = currentGenre.trimmed().toLower();
    QString g2 = candidateGenre.trimmed().toLower();

    if (g1.isEmpty() || g2.isEmpty()) {
        return 0.5; // Neutral if either track has empty genre
    }

    if (g1 == g2) {
        return 1.0; // Exact match
    }

    // Split compound genres by common delimiters
    auto tokenize = [](const QString& genreStr) {
        QSet<QString> tokens;
        const QStringList parts = genreStr.split(
                QRegularExpression(QStringLiteral("[/\\-,;&|]")),
                Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            QString clean = part.trimmed();
            if (!clean.isEmpty()) {
                tokens.insert(clean);
            }
        }
        return tokens;
    };

    QSet<QString> tokens1 = tokenize(g1);
    QSet<QString> tokens2 = tokenize(g2);

    if (tokens1.isEmpty() || tokens2.isEmpty()) {
        return 0.3;
    }

    int intersectionSize = 0;
    for (const QString& t1 : tokens1) {
        for (const QString& t2 : tokens2) {
            if (t1 == t2 || t1.contains(t2) || t2.contains(t1)) {
                intersectionSize++;
                break;
            }
        }
    }

    if (intersectionSize > 0) {
        int unionSize = tokens1.size() + tokens2.size() - intersectionSize;
        double jaccard = static_cast<double>(intersectionSize) / std::max(1, unionSize);
        return std::clamp(0.6 + (0.4 * jaccard), 0.0, 1.0);
    }

    return 0.25; // Different genres
}

double SmartAutoDJMatcher::calculateEnergyScore(
        const TrackPointer& pCurrentTrack,
        const TrackPointer& pCandidateTrack) {
    if (!pCurrentTrack || !pCandidateTrack) {
        return 0.5;
    }

    // ReplayGain energy comparison
    double r1 = pCurrentTrack->getReplayGain().getRatio();
    double r2 = pCandidateTrack->getReplayGain().getRatio();

    if (r1 > 0.0 && r2 > 0.0) {
        double ratio = (r1 > r2) ? (r2 / r1) : (r1 / r2);
        return std::clamp(ratio, 0.2, 1.0);
    }

    return 0.5;
}

SmartAutoDJMatcher::MatchScores SmartAutoDJMatcher::evaluateCandidate(
        const TrackPointer& pCurrentTrack,
        const TrackPointer& pCandidateTrack,
        const MatchWeights& weights) {
    MatchScores scores;
    if (!pCurrentTrack || !pCandidateTrack) {
        return scores;
    }

    scores.keyScore = calculateHarmonicScore(
            pCurrentTrack->getKey(),
            pCandidateTrack->getKey());

    scores.bpmScore = calculateBpmScore(
            pCurrentTrack->getBpm(),
            pCandidateTrack->getBpm(),
            weights.maxBpmDeltaPercent,
            weights.allowHalfDoubleBpm);

    scores.genreScore = calculateGenreScore(
            pCurrentTrack->getGenre(),
            pCandidateTrack->getGenre());

    scores.energyScore = calculateEnergyScore(
            pCurrentTrack,
            pCandidateTrack);

    double totalWeight = weights.keyWeight + weights.bpmWeight +
            weights.genreWeight + weights.energyWeight;
    if (totalWeight <= 0.0) {
        totalWeight = 1.0;
    }

    scores.totalScore = (scores.keyScore * weights.keyWeight +
            scores.bpmScore * weights.bpmWeight +
            scores.genreScore * weights.genreWeight +
            scores.energyScore * weights.energyWeight) / totalWeight;

    // Build explanatory string for UI / logs
    scores.explanation = QStringLiteral("Score: %1% (Key: %2%, BPM: %3%, Genre: %4%)")
            .arg(static_cast<int>(scores.totalScore * 100))
            .arg(static_cast<int>(scores.keyScore * 100))
            .arg(static_cast<int>(scores.bpmScore * 100))
            .arg(static_cast<int>(scores.genreScore * 100));

    return scores;
}

QList<QPair<TrackPointer, SmartAutoDJMatcher::MatchScores>> SmartAutoDJMatcher::rankCandidates(
        const TrackPointer& pCurrentTrack,
        const QList<TrackPointer>& candidates,
        const MatchWeights& weights) {
    QList<QPair<TrackPointer, MatchScores>> ranked;
    ranked.reserve(candidates.size());

    for (const auto& pCandidate : candidates) {
        if (!pCandidate) {
            continue;
        }
        MatchScores scores = evaluateCandidate(pCurrentTrack, pCandidate, weights);
        ranked.append(qMakePair(pCandidate, scores));
    }

    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.second.totalScore > b.second.totalScore;
    });

    return ranked;
}
