#pragma once

#include <array>
#include <cstdint>
#include <utility>

namespace dkr::runtime::audio {

constexpr float clamp_eq_gain(float decibels) {
    return decibels < -12.0F ? -12.0F : decibels > 12.0F ? 12.0F : decibels;
}

class StereoEqualizer {
  public:
    void configure(std::uint32_t sample_rate, float bass_db, float mid_db, float treble_db);
    void reset();
    bool neutral() const;
    std::pair<float, float> process(float left, float right);

    struct Biquad {
        float b0 = 1.0F;
        float b1 = 0.0F;
        float b2 = 0.0F;
        float a1 = 0.0F;
        float a2 = 0.0F;
        float z1 = 0.0F;
        float z2 = 0.0F;

        float process(float input);
        void clear();
    };

  private:
    std::array<Biquad, 3> left_{};
    std::array<Biquad, 3> right_{};
    std::uint32_t sample_rate_ = 0;
    float bass_db_ = 0.0F;
    float mid_db_ = 0.0F;
    float treble_db_ = 0.0F;
    bool neutral_ = true;
};

} // namespace dkr::runtime::audio
