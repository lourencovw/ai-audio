#include "AudioAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace aiaudio {

double linearToDbfs(double linear) {
    if (linear <= 0.0) return kSilenceFloorDbfs;
    double db = 20.0 * std::log10(linear);
    return std::max(db, kSilenceFloorDbfs);
}

AudioAnalyzer::AudioAnalyzer(uint32_t sampleRate, double blockDurationMs)
    : sampleRate_(sampleRate),
      blockSize_(std::max<size_t>(1, static_cast<size_t>(sampleRate * blockDurationMs / 1000.0))) {
    blockBuffer_.reserve(blockSize_);
}

void AudioAnalyzer::feed(const float *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float s = samples[i];
        peakLinear_ = std::max(peakLinear_, static_cast<double>(std::fabs(s)));
        sumSquares_ += static_cast<double>(s) * static_cast<double>(s);
        ++totalSamples_;

        blockBuffer_.push_back(s);
        if (blockBuffer_.size() >= blockSize_) flushBlock();
    }
}

void AudioAnalyzer::flushBlock() {
    double sumSq = 0.0;
    for (float s : blockBuffer_) sumSq += static_cast<double>(s) * static_cast<double>(s);
    double rms = std::sqrt(sumSq / static_cast<double>(blockBuffer_.size()));
    blockRmsDbfs_.push_back(linearToDbfs(rms));
    blockBuffer_.clear();
}

LevelStats AudioAnalyzer::finish() const {
    LevelStats stats;
    stats.sampleCount = totalSamples_;
    stats.peakDbfs = linearToDbfs(peakLinear_);

    if (totalSamples_ > 0) {
        double rms = std::sqrt(sumSquares_ / static_cast<double>(totalSamples_));
        stats.rmsDbfsAvg = linearToDbfs(rms);
    }

    // Include a partial trailing block (shorter than blockSize_) so short
    // stages (e.g. three claps) still contribute at least one sample to the
    // percentile estimate.
    std::vector<double> blocks = blockRmsDbfs_;
    if (!blockBuffer_.empty()) {
        double sumSq = 0.0;
        for (float s : blockBuffer_) sumSq += static_cast<double>(s) * static_cast<double>(s);
        blocks.push_back(linearToDbfs(std::sqrt(sumSq / static_cast<double>(blockBuffer_.size()))));
    }

    if (!blocks.empty()) {
        std::vector<double> sorted = blocks;
        std::sort(sorted.begin(), sorted.end());
        stats.rmsDbfsMin = sorted.front();
        stats.rmsDbfsMax = sorted.back();

        size_t idx = static_cast<size_t>(std::floor(0.10 * static_cast<double>(sorted.size() - 1)));
        stats.noiseFloorDbfs = sorted[idx];
    }

    return stats;
}

} // namespace aiaudio
