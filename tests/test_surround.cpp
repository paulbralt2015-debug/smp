#include "SurroundProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("ECHEC ligne %d : %s\n", __LINE__, #cond);       \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static const float kPi = 3.14159265358979f;
static const int kRate = 48000;

static std::vector<float> sine(int frames, float freq, float ampL, float ampR)
{
    std::vector<float> in(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = std::sin(2.0f * kPi * freq * i / kRate);
        in[i * 2] = ampL * s;
        in[i * 2 + 1] = ampR * s;
    }
    return in;
}

static float peak(const std::vector<float> &out, int channels, int channel, int fromFrame)
{
    float p = 0.0f;
    const int frames = static_cast<int>(out.size()) / channels;
    for (int i = fromFrame; i < frames; ++i)
        p = std::max(p, std::fabs(out[i * channels + channel]));
    return p;
}

static void testChannelCounts()
{
    const auto in = sine(1000, 440.0f, 0.5f, 0.5f);
    std::vector<float> out;

    SurroundProcessor p(kRate);

    p.setMode(SurroundProcessor::Stereo);
    p.process(in.data(), 1000, out);
    CHECK(out.size() == 2000);

    p.setMode(SurroundProcessor::Surround51);
    p.process(in.data(), 1000, out);
    CHECK(out.size() == 6000);

    p.setMode(SurroundProcessor::Surround71);
    p.process(in.data(), 1000, out);
    CHECK(out.size() == 8000);
}

static void testInvalidInput()
{
    SurroundProcessor p(kRate);
    std::vector<float> out(10, 1.0f);

    p.process(nullptr, 100, out);
    CHECK(out.empty());

    const float dummy[2] = {0.0f, 0.0f};
    out.assign(10, 1.0f);
    p.process(dummy, 0, out);
    CHECK(out.empty());
}

static void testStereoPassthrough()
{
    const auto in = sine(500, 1000.0f, 0.7f, 0.3f);
    std::vector<float> out;

    SurroundProcessor p(kRate);
    p.setMode(SurroundProcessor::Stereo);
    p.process(in.data(), 500, out);

    bool identical = out.size() == in.size();
    for (size_t i = 0; identical && i < in.size(); ++i)
        identical = out[i] == in[i];

    CHECK(identical);
}

static void testSilence()
{
    const std::vector<float> in(2000, 0.0f);
    std::vector<float> out;

    SurroundProcessor p(kRate);
    p.process(in.data(), 1000, out);

    CHECK(peak(out, 6, 0, 0) == 0.0f);
    CHECK(peak(out, 6, 3, 0) == 0.0f);
    CHECK(peak(out, 6, 4, 0) == 0.0f);
}

static void testCenterOnlyHasNoRear()
{
    // Signal identique à gauche et à droite : rien dans les arrières.
    const auto in = sine(4800, 1000.0f, 0.5f, 0.5f);
    std::vector<float> out;

    SurroundProcessor p(kRate);
    p.setMode(SurroundProcessor::Surround51);
    p.process(in.data(), 4800, out);

    CHECK(peak(out, 6, 4, 0) < 1e-6f);
    CHECK(peak(out, 6, 5, 0) < 1e-6f);
    CHECK(peak(out, 6, 2, 0) > 0.3f);
}

static void testSideSignalGoesToRear()
{
    // Signal uniquement à gauche : de l'énergie dans les arrières,
    // et rien dans le centre.
    const auto in = sine(4800, 1000.0f, 0.5f, 0.0f);
    std::vector<float> out;

    SurroundProcessor p(kRate);
    p.setMode(SurroundProcessor::Surround51);
    p.process(in.data(), 4800, out);

    CHECK(peak(out, 6, 4, 1000) > 0.05f);
    CHECK(peak(out, 6, 5, 1000) > 0.05f);
}

static void testRearIsDelayed()
{
    // Impulsion à gauche : le premier échantillon non nul des arrières
    // doit arriver après le délai (15 ms = 720 échantillons à 48 kHz).
    std::vector<float> in(4800 * 2, 0.0f);
    in[0] = 1.0f;

    std::vector<float> out;
    SurroundProcessor p(kRate);
    p.setMode(SurroundProcessor::Surround51);
    p.process(in.data(), 4800, out);

    int first = -1;
    for (int i = 0; i < 4800; ++i) {
        if (std::fabs(out[i * 6 + 4]) > 1e-4f) {
            first = i;
            break;
        }
    }

    CHECK(first >= 700);
}

static void testLfeFilter()
{
    std::vector<float> out;
    SurroundProcessor p(kRate);
    p.setMode(SurroundProcessor::Surround51);

    // 50 Hz : doit passer dans le LFE.
    auto low = sine(kRate, 50.0f, 0.5f, 0.5f);
    p.process(low.data(), kRate, out);
    CHECK(peak(out, 6, 3, kRate - 4800) > 0.3f);

    // 5 kHz : doit être quasiment éliminé.
    p.reset();
    auto high = sine(kRate, 5000.0f, 0.5f, 0.5f);
    p.process(high.data(), kRate, out);
    CHECK(peak(out, 6, 3, kRate - 4800) < 0.01f);
}

static void testNoClipping()
{
    // Signal plein échelle : aucune sortie ne doit atteindre ±1.
    std::vector<float> in(4000, 1.0f);
    std::vector<float> out;

    SurroundProcessor p(kRate);
    p.setCenterLevel(1.5f);
    p.setSurroundLevel(1.5f);
    p.setLfeLevel(1.5f);

    for (auto mode : {SurroundProcessor::Surround51, SurroundProcessor::Surround71}) {
        p.setMode(mode);
        p.process(in.data(), 2000, out);

        const int ch = p.outputChannels();
        float maxAbs = 0.0f;
        for (float v : out)
            maxAbs = std::max(maxAbs, std::fabs(v));

        CHECK(maxAbs < 1.0f);
        CHECK(static_cast<int>(out.size()) == 2000 * ch);
    }
}

int main()
{
    testChannelCounts();
    testInvalidInput();
    testStereoPassthrough();
    testSilence();
    testCenterOnlyHasNoRear();
    testSideSignalGoesToRear();
    testRearIsDelayed();
    testLfeFilter();
    testNoClipping();

    if (failures == 0)
        std::printf("Tous les tests passent.\n");

    return failures == 0 ? 0 : 1;
}
