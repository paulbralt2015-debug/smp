#include "SurroundProcessor.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kK = 0.70710678f;
constexpr float kButterworthQ = 0.70710678f;

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

float softLimit(float x)
{
    constexpr float threshold = 0.8f;
    const float magnitude = std::fabs(x);
    if (magnitude <= threshold) return x;
    const float knee = 1.0f - threshold;
    return std::copysign(threshold + knee * std::tanh((magnitude - threshold) / knee), x);
}
}

void SurroundProcessor::Biquad::setLowPass(float cutoff, float rate, float q)
{
    const float w0 = 2.0f * kPi * cutoff / rate;
    const float c = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    b0 = (1.0f - c) * 0.5f / a0;
    b1 = (1.0f - c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
}

void SurroundProcessor::DelayLine::resize(std::size_t n)
{
    buffer.assign(std::max<std::size_t>(n, 1), 0.0f);
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

SurroundProcessor::SurroundProcessor(int rate) { setSampleRate(rate); }

void SurroundProcessor::setSampleRate(int rate)
{
    sampleRate = std::max(8000, rate);
    const float fs = static_cast<float>(sampleRate);
    lfeLow1.setLowPass(std::min(120.0f, fs * 0.45f), fs, kButterworthQ);
    lfeLow2.setLowPass(std::min(120.0f, fs * 0.45f), fs, kButterworthQ);
    surroundLow.setLowPass(std::min(7000.0f, fs * 0.45f), fs, kButterworthQ);
    const auto samples = [fs](float seconds) { return static_cast<std::size_t>(std::lround(seconds * fs)); };
    sideDelay = samples(0.010f);
    rearDelay = samples(surroundDelayMs / 1000.0f);
    backDelay = samples(0.022f);
    delayA.resize(samples(0.080f) + 2);
    delayB.resize(samples(0.080f) + 2);
    delayC.resize(samples(0.080f) + 2);
    delayD.resize(samples(0.080f) + 2);
    reset();
}

void SurroundProcessor::setSurroundDelay(float ms)
{
    surroundDelayMs = clampValue(ms, 0.0f, 30.0f);
    rearDelay = static_cast<std::size_t>(std::lround(surroundDelayMs * sampleRate / 1000.0f));
    reset();
}

void SurroundProcessor::setMode(Mode value) { currentMode = value; reset(); }

void SurroundProcessor::setProfile(Profile profile)
{
    listeningProfile = profile;
    switch (profile) {
    case Night:
        setCenterLevel(1.15f); setSurroundLevel(0.45f); setLfeLevel(0.35f);
        setDimension(-1.0f); setCenterWidth(2.0f); setPanorama(false); break;
    case Game:
        setCenterLevel(1.0f); setSurroundLevel(1.20f); setLfeLevel(0.90f);
        setDimension(1.0f); setCenterWidth(3.0f); setPanorama(true); break;
    case Movie:
        setCenterLevel(1.0f); setSurroundLevel(0.90f); setLfeLevel(0.90f);
        setDimension(0.0f); setCenterWidth(0.0f); setPanorama(false); break;
    case Music:
        setCenterLevel(0.90f); setSurroundLevel(0.65f); setLfeLevel(0.65f);
        setDimension(0.0f); setCenterWidth(3.0f); setPanorama(false); break;
    case Standard:
    default:
        setCenterLevel(1.0f); setSurroundLevel(0.75f); setLfeLevel(0.80f);
        setDimension(0.0f); setCenterWidth(0.0f); setPanorama(false); break;
    }
}

void SurroundProcessor::setSurroundLevel(float v) { surroundGain = clampValue(v, 0.0f, 1.5f); }
void SurroundProcessor::setCenterLevel(float v) { centerGain = clampValue(v, 0.0f, 1.5f); }
void SurroundProcessor::setLfeLevel(float v) { lfeGain = clampValue(v, 0.0f, 1.5f); }
void SurroundProcessor::setInputGain(float v) { inputGain = clampValue(v, 0.0f, 3.0f); }
int SurroundProcessor::outputChannels() const { return static_cast<int>(currentMode); }

void SurroundProcessor::reset()
{
    lfeLow1.clear(); lfeLow2.clear(); surroundLow.clear();
    delayA.clear(); delayB.clear(); delayC.clear(); delayD.clear();
    surroundAllPassA.clear(); surroundAllPassB.clear();
    midEnvelope = sideEnvelope = normalizerEnvelope = 0.0f;
}

void SurroundProcessor::process(const float *input, int frames, std::vector<float> &output)
{
    output.clear();
    if (!input || frames <= 0) return;
    const int channels = outputChannels();
    output.resize(static_cast<std::size_t>(frames) * channels);

    const bool classic = matrixMode == DolbySurround || matrixMode == ProLogic;
    const bool music = listeningProfile == Music;
    const float frontGain = dbToLinear(-dimension * 1.5f);
    const float rearGain = dbToLinear(dimension * 1.5f);
    const float width = music ? (centerWidth / 7.0f * 0.5f) : 0.0f;
    const float panoramaMix = (music && panorama) ? 0.5f : 0.0f;

    for (int i = 0; i < frames; ++i) {
        float left = input[i * 2] * inputGain;
        float right = input[i * 2 + 1] * inputGain;

        if (voiceNormalization) {
            const float mono = 0.5f * (left + right);
            const float level = std::fabs(mono);
            const float coefficient = level > normalizerEnvelope ? 0.015f : 0.00035f;
            normalizerEnvelope += coefficient * (level - normalizerEnvelope);
            const float correction = clampValue(0.178f / std::max(normalizerEnvelope, 0.025f), 0.5f, 2.5f);
            left *= correction; right *= correction;
        }

        if (currentMode == Stereo) {
            output[i * 2] = softLimit(left);
            output[i * 2 + 1] = softLimit(right);
            continue;
        }

        const float mid = (left + right) * 0.5f;
        const float side = (left - right) * 0.5f;
        midEnvelope += 0.0025f * (std::fabs(mid) - midEnvelope);
        sideEnvelope += 0.0025f * (std::fabs(side) - sideEnvelope);
        const float total = midEnvelope + sideEnvelope + 1.0e-5f;
        const float centerSteering = clampValue((midEnvelope - sideEnvelope * 0.35f) / total, 0.0f, 1.0f);
        const float surroundSteering = clampValue((sideEnvelope - midEnvelope * 0.25f) / total, 0.0f, 1.0f);

        float frontLeft = left, frontRight = right;
        float center = 0.0f;
        float rearL = 0.0f, rearR = 0.0f;
        if (classic) {
            // Matrice passive du projet source : centre = L+R, arrière mono = L-R.
            center = (kK * left + kK * right) * centerGain;
            const float rear = surroundLow.process((kK * left - kK * right) * surroundGain);
            rearL = rearR = rear;
        } else {
            // Décodage adaptatif Movie/Music, inspiré du filtre FFmpeg surround.
            const float decodedCenter = mid * (0.85f + 0.15f * centerSteering);
            center = decodedCenter * (1.0f - width) * centerGain * frontGain;
            frontLeft = (left + width * kK * decodedCenter) * frontGain
                      - mid * 0.20f * centerSteering;
            frontRight = (right + width * kK * decodedCenter) * frontGain
                       - mid * 0.20f * centerSteering;

            float matrixSignal = side;
            const float steering = 0.75f + 0.5f * surroundSteering;
            matrixSignal *= surroundGain * steering * rearGain;
            const float filtered = surroundLow.process(matrixSignal);
            const float delayedL = delayA.process(surroundAllPassA.process(filtered), rearDelay);
            const float delayedR = delayB.process(surroundAllPassB.process(filtered), rearDelay + sideDelay / 3);
            rearL = delayedL + panoramaMix * side * rearGain;
            rearR = delayedR - panoramaMix * side * rearGain;
        }

        const float lfeInput = subwoofer ? (kK * 0.5f * left + kK * 0.5f * right) : 0.0f;
        const float lfe = lfeLow2.process(lfeLow1.process(lfeInput)) * lfeGain;
        float *out = &output[static_cast<std::size_t>(i) * channels];
        out[0] = softLimit(frontLeft);
        out[1] = softLimit(frontRight);
        out[2] = softLimit(center);
        out[3] = softLimit(lfe);

        if (currentMode == Surround51) {
            if (classic) {
                out[4] = softLimit(delayA.process(rearL, rearDelay));
                out[5] = softLimit(delayB.process(rearR, rearDelay));
            } else {
                out[4] = softLimit(rearL);
                out[5] = softLimit(rearR);
            }
        } else {
            const float decorA = surroundAllPassA.process(rearL);
            const float decorB = surroundAllPassB.process(rearR);
            out[4] = softLimit(delayC.process(decorA, backDelay) * 0.45f);
            out[5] = softLimit(delayD.process(decorB, backDelay + sideDelay) * 0.45f);
            out[6] = softLimit(delayA.process(rearL, sideDelay) * 0.70f);
            out[7] = softLimit(delayB.process(rearR, sideDelay + 96) * 0.70f);
        }
    }
}
