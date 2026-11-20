#include "SurroundProcessor.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kButterworthQ = 0.70710678f;

// Limiteur doux : transparent sous 0,8, puis compression progressive.
// La sortie reste strictement inférieure à 1.
float softLimit(float x)
{
    constexpr float threshold = 0.8f;
    const float magnitude = std::fabs(x);

    if (magnitude <= threshold)
        return x;

    const float over = magnitude - threshold;
    const float knee = 1.0f - threshold;
    const float limited = threshold + knee * std::tanh(over / knee);

    return std::copysign(limited, x);
}

} // namespace

void SurroundProcessor::Biquad::setLowPass(float cutoff, float rate, float q)
{
    // Filtre passe-bas du second ordre (cookbook RBJ).
    const float w0 = 2.0f * kPi * cutoff / rate;
    const float cosW = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;

    b0 = (1.0f - cosW) * 0.5f / a0;
    b1 = (1.0f - cosW) / a0;
    b2 = b0;
    a1 = -2.0f * cosW / a0;
    a2 = (1.0f - alpha) / a0;
}

void SurroundProcessor::DelayLine::resize(std::size_t samples)
{
    buffer.assign(std::max<std::size_t>(samples, 1), 0.0f);
    pos = 0;
}

void SurroundProcessor::DelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    pos = 0;
}

float SurroundProcessor::DelayLine::process(float x, std::size_t delay)
{
    const std::size_t n = buffer.size();
    delay = std::min(delay, n - 1);

    buffer[pos] = x;
    const float y = buffer[(pos + n - delay) % n];
    pos = (pos + 1) % n;

    return y;
}

SurroundProcessor::SurroundProcessor(int rate)
{
    setSampleRate(rate);
}

void SurroundProcessor::setSampleRate(int rate)
{
    sampleRate = std::max(8000, rate);

    const float fs = static_cast<float>(sampleRate);

    // LFE : Linkwitz-Riley d'ordre 4 = deux Butterworth d'ordre 2 en série.
    const float lfeCutoff = std::min(120.0f, fs * 0.45f);
    lfeLow1.setLowPass(lfeCutoff, fs, kButterworthQ);
    lfeLow2.setLowPass(lfeCutoff, fs, kButterworthQ);

    // Surround : coupure à environ 7 kHz.
    const float surroundCutoff = std::min(7000.0f, fs * 0.45f);
    surroundLow.setLowPass(surroundCutoff, fs, kButterworthQ);

    auto samples = [fs](float seconds) {
        return static_cast<std::size_t>(std::lround(seconds * fs));
    };

    sideDelay = samples(0.010f);
    rearDelay = samples(0.015f);
    backDelay = samples(0.022f);

    delayA.resize(samples(0.050f) + 2);
    delayB.resize(samples(0.050f) + 2);

    reset();
}

void SurroundProcessor::setMode(Mode newMode)
{
    currentMode = newMode;
    reset();
}

void SurroundProcessor::setProfile(Profile profile)
{
    listeningProfile = profile;
    switch (profile) {
    case Night:  setCenterLevel(1.15f); setSurroundLevel(0.45f); setLfeLevel(0.35f); break;
    case Game:   setCenterLevel(1.0f);  setSurroundLevel(1.20f); setLfeLevel(0.90f); break;
    case Movie:  setCenterLevel(1.0f);  setSurroundLevel(0.90f); setLfeLevel(0.90f); break;
    case Music:  setCenterLevel(0.90f); setSurroundLevel(0.65f); setLfeLevel(0.65f); break;
    case Standard: default:
        setCenterLevel(1.0f); setSurroundLevel(0.75f); setLfeLevel(0.80f); break;
    }
}

void SurroundProcessor::setSurroundLevel(float level)
{
    surroundGain = std::clamp(level, 0.0f, 1.5f);
}

void SurroundProcessor::setCenterLevel(float level)
{
    centerGain = std::clamp(level, 0.0f, 1.5f);
}

void SurroundProcessor::setLfeLevel(float level)
{
    lfeGain = std::clamp(level, 0.0f, 1.5f);
}

int SurroundProcessor::outputChannels() const
{
    return static_cast<int>(currentMode);
}

void SurroundProcessor::reset()
{
    lfeLow1.clear();
    lfeLow2.clear();
    surroundLow.clear();
    delayA.clear();
    delayB.clear();
}

void SurroundProcessor::process(
    const float *input,
    int frames,
    std::vector<float> &output)
{
    output.clear();

    if (!input || frames <= 0)
        return;

    const int channels = outputChannels();
    output.resize(static_cast<std::size_t>(frames) * channels);

    // Une partie du centre est retirée des fronts : sinon les dialogues
    // seraient comptés deux fois (fronts + canal central).
    const float frontMix = 0.5f * std::min(centerGain, 1.0f);

    for (int i = 0; i < frames; ++i) {
        const float left = input[i * 2];
        const float right = input[i * 2 + 1];

        if (currentMode == Stereo) {
            output[i * 2] = left;
            output[i * 2 + 1] = right;
            continue;
        }

        const float mid = (left + right) * 0.5f;
        const float side = (left - right) * 0.5f;

        const float center = mid * centerGain;
        const float lfe = lfeLow2.process(lfeLow1.process(mid)) * lfeGain;
        float matrixSignal = side;
        if (matrixMode == DolbySurround || matrixMode == ProLogic)
            matrixSignal = (left - right) * 0.25f;
        const float surroundSignal = surroundLow.process(matrixSignal * surroundGain);

        const float frontLeft = left - mid * frontMix;
        const float frontRight = right - mid * frontMix;

        float *out = &output[static_cast<std::size_t>(i) * channels];

        out[0] = softLimit(frontLeft);
        out[1] = softLimit(frontRight);
        out[2] = softLimit(center);
        out[3] = softLimit(lfe);

        if (currentMode == Surround51) {
            const float rear = delayA.process(surroundSignal, rearDelay);
            if (matrixMode == DolbySurround || matrixMode == ProLogic) {
                // Décodage matriciel mono-arrière de type Pro Logic classique.
                out[4] = softLimit(rear);
                out[5] = softLimit(rear);
            } else {
                out[4] = softLimit(rear);
                out[5] = softLimit(-rear);
            }
        }
        else {
            const float sideSignal = delayA.process(surroundSignal, sideDelay);
            const float backSignal = delayB.process(surroundSignal, backDelay);

            out[4] = softLimit(backSignal * 0.5f);
            out[5] = softLimit(-backSignal * 0.5f);
            out[6] = softLimit(sideSignal * 0.7f);
            out[7] = softLimit(-sideSignal * 0.7f);
        }
    }
}
