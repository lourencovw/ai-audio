#pragma once
#include <cstddef>
#include <cstdint>

namespace aiaudio {

// A read-only view over a block of mono, 32-bit float PCM samples in the
// range [-1.0, 1.0]. The core calibration engine only ever consumes mono
// audio; the OBS integration layer is responsible for downmixing
// multi-channel input before calling into the engine.
struct AudioBlock {
    const float *samples = nullptr;
    size_t count = 0;
    uint32_t sampleRate = 48000;
};

// Floor value used whenever a true dBFS computation would be -infinity
// (e.g. exact digital silence), so metrics stay well-defined and
// comparable instead of propagating -inf/NaN.
constexpr double kSilenceFloorDbfs = -120.0;

} // namespace aiaudio
