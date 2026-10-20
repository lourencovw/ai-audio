#pragma once
#include "AudioTypes.h"
#include <vector>

namespace aiaudio {

struct LevelStats {
    double rmsDbfsAvg = kSilenceFloorDbfs;   // energy-averaged RMS across the whole stage
    double rmsDbfsMin = kSilenceFloorDbfs;   // quietest ~20ms analysis block
    double rmsDbfsMax = kSilenceFloorDbfs;   // loudest ~20ms analysis block
    double peakDbfs = kSilenceFloorDbfs;     // single loudest sample
    double noiseFloorDbfs = kSilenceFloorDbfs; // 10th percentile of block RMS
    size_t sampleCount = 0;
};

// Converts a linear amplitude (0.0-1.0+) to dBFS, clamped at kSilenceFloorDbfs
// so exact silence (linear == 0) does not produce -infinity.
double linearToDbfs(double linear);

// Accumulates level statistics over a stream of audio fed in arbitrarily
// sized chunks. Internally slices the stream into fixed-size analysis
// blocks (~20ms) so that noise-floor percentile estimation reflects the
// quiet gaps between words/claps/keystrokes rather than a single
// whole-stage average.
class AudioAnalyzer {
public:
    explicit AudioAnalyzer(uint32_t sampleRate, double blockDurationMs = 20.0);

    void feed(const float *samples, size_t count);

    // Computes stats from everything fed so far. Does not reset internal
    // state, so it is safe to call more than once (e.g. for inspection).
    LevelStats finish() const;

private:
    void flushBlock();

    uint32_t sampleRate_;
    size_t blockSize_;
    std::vector<float> blockBuffer_;
    std::vector<double> blockRmsDbfs_;

    double peakLinear_ = 0.0;
    double sumSquares_ = 0.0; // across the whole stage, for the energy-average RMS
    size_t totalSamples_ = 0;
};

} // namespace aiaudio
