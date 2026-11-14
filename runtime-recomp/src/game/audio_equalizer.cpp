#include "audio_equalizer.hpp"

#include <algorithm>
#include <cmath>

namespace {

using Biquad = dkr::runtime::audio::StereoEqualizer::Biquad;
constexpr double kPi = 3.14159265358979323846;

void Normalise(Biquad& filter, double b0, double b1, double b2, double a0, double a1, double a2) {
    filter.b0 = static_cast<float>(b0 / a0);
    filter.b1 = static_cast<float>(b1 / a0);
    filter.b2 = static_cast<float>(b2 / a0);
    filter.a1 = static_cast<float>(a1 / a0);
    filter.a2 = static_cast<float>(a2 / a0);
    filter.clear();
}

void ConfigurePeak(Biquad& filter, double sample_rate, double frequency, double q, double gain_db) {
    const double a = std::pow(10.0, gain_db / 40.0);
    const double omega = 2.0 * kPi * frequency / sample_rate;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double cosine = std::cos(omega);
    Normalise(filter, 1.0 + alpha * a, -2.0 * cosine, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cosine, 1.0 - alpha / a);
}

void ConfigureShelf(Biquad& filter, double sample_rate, double frequency, double gain_db, bool high) {
    const double a = std::pow(10.0, gain_db / 40.0);
    const double omega = 2.0 * kPi * frequency / sample_rate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine * std::sqrt(2.0) / 2.0;
    const double root = 2.0 * std::sqrt(a) * alpha;
    if (high) {
        Normalise(filter, a * ((a + 1.0) + (a - 1.0) * cosine + root), -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine),
                  a * ((a + 1.0) + (a - 1.0) * cosine - root), (a + 1.0) - (a - 1.0) * cosine + root,
                  2.0 * ((a - 1.0) - (a + 1.0) * cosine), (a + 1.0) - (a - 1.0) * cosine - root);
    } else {
        Normalise(filter, a * ((a + 1.0) - (a - 1.0) * cosine + root), 2.0 * a * ((a - 1.0) - (a + 1.0) * cosine),
                  a * ((a + 1.0) - (a - 1.0) * cosine - root), (a + 1.0) + (a - 1.0) * cosine + root,
                  -2.0 * ((a - 1.0) + (a + 1.0) * cosine), (a + 1.0) + (a - 1.0) * cosine - root);
    }
}

} // namespace

float dkr::runtime::audio::StereoEqualizer::Biquad::process(float input) {
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void dkr::runtime::audio::StereoEqualizer::Biquad::clear() {
    z1 = 0.0F;
    z2 = 0.0F;
}

void dkr::runtime::audio::StereoEqualizer::configure(std::uint32_t sample_rate, float bass_db, float mid_db,
                                                     float treble_db) {
    sample_rate = std::clamp(sample_rate, 8000U, 192000U);
    bass_db = clamp_eq_gain(bass_db);
    mid_db = clamp_eq_gain(mid_db);
    treble_db = clamp_eq_gain(treble_db);
    if (sample_rate == sample_rate_ && bass_db == bass_db_ && mid_db == mid_db_ && treble_db == treble_db_) {
        return;
    }
    sample_rate_ = sample_rate;
    bass_db_ = bass_db;
    mid_db_ = mid_db;
    treble_db_ = treble_db;
    neutral_ = std::fabs(bass_db) < 0.001F && std::fabs(mid_db) < 0.001F && std::fabs(treble_db) < 0.001F;
    ConfigureShelf(left_[0], sample_rate, 120.0, bass_db, false);
    ConfigurePeak(left_[1], sample_rate, 1000.0, 0.8, mid_db);
    ConfigureShelf(left_[2], sample_rate, 6000.0, treble_db, true);
    right_ = left_;
}

void dkr::runtime::audio::StereoEqualizer::reset() {
    for (auto& filter : left_)
        filter.clear();
    for (auto& filter : right_)
        filter.clear();
}

bool dkr::runtime::audio::StereoEqualizer::neutral() const {
    return neutral_;
}

std::pair<float, float> dkr::runtime::audio::StereoEqualizer::process(float left, float right) {
    if (neutral_) {
        return {left, right};
    }
    for (auto& filter : left_)
        left = filter.process(left);
    for (auto& filter : right_)
        right = filter.process(right);
    return {left, right};
}
