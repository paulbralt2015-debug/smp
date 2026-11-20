#pragma once

#include <cstddef>
#include <vector>

// Moteur matriciel inspiré du projet lecteur_prologic1.
// Traitement stéréo -> 5.1 / 7.1, avec profils Movie/Music et matrice classique.
// Ce traitement ouvert n'est pas un décodeur Dolby certifié.
class SurroundProcessor
{
public:
    enum Mode { Stereo = 2, Surround51 = 6, Surround71 = 8 };
    enum MatrixMode { DolbySurround = 0, ProLogic = 1, ProLogicII = 2, ProLogicIIx = 3 };
    enum Profile { Standard = 0, Night = 1, Game = 2, Movie = 3, Music = 4 };

    explicit SurroundProcessor(int sampleRate = 48000);
    void setSampleRate(int sampleRate);
    int sampleRateHz() const { return sampleRate; }
    void setMode(Mode mode);
    Mode mode() const { return currentMode; }
    void setMatrixMode(MatrixMode mode) { matrixMode = mode; reset(); }
    MatrixMode matrixModeValue() const { return matrixMode; }
    void setProfile(Profile profile);
    void setSurroundLevel(float level);
    void setCenterLevel(float level);
    void setLfeLevel(float level);
    void setInputGain(float linearGain);
    void setVoiceNormalization(bool enabled) { voiceNormalization = enabled; normalizerEnvelope = 0.0f; }
    void setCenterWidth(float value) { centerWidth = clampValue(value, 0.0f, 7.0f); }
    void setDimension(float value) { dimension = clampValue(value, -3.0f, 3.0f); }
    void setPanorama(bool enabled) { panorama = enabled; }
    void setSubwoofer(bool enabled) { subwoofer = enabled; }
    void setSurroundDelay(float milliseconds);

    float surroundLevel() const { return surroundGain; }
    float centerLevel() const { return centerGain; }
    float lfeLevel() const { return lfeGain; }
    int outputChannels() const;
    void process(const float *input, int frames, std::vector<float> &output);
    void reset();

private:
    static float clampValue(float value, float low, float high)
    {
        return value < low ? low : value > high ? high : value;
    }

    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        void setLowPass(float cutoff, float rate, float q);
        float process(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1 = z2 = 0.0f; }
    };

    struct AllPass {
        float coefficient = 0.65f;
        float previousInput = 0.0f, previousOutput = 0.0f;
        float process(float x)
        {
            const float y = -coefficient * x + previousInput + coefficient * previousOutput;
            previousInput = x; previousOutput = y; return y;
        }
        void clear() { previousInput = previousOutput = 0.0f; }
    };

    struct DelayLine {
        std::vector<float> buffer;
        std::size_t pos = 0;
        void resize(std::size_t samples);
        void clear();
        float process(float x, std::size_t delay);
    };

    int sampleRate = 48000;
    Mode currentMode = Surround51;
    MatrixMode matrixMode = ProLogicII;
    Profile listeningProfile = Movie;
    float surroundGain = 0.75f, centerGain = 1.0f, lfeGain = 0.8f, inputGain = 1.0f;
    float normalizerEnvelope = 0.0f;
    bool voiceNormalization = false;
    float centerWidth = 3.0f;
    float dimension = 0.0f;
    float surroundDelayMs = 10.0f;
    bool panorama = false;
    bool subwoofer = false;

    Biquad lfeLow1, lfeLow2, surroundLow;
    DelayLine delayA, delayB, delayC, delayD;
    AllPass surroundAllPassA, surroundAllPassB;
    float midEnvelope = 0.0f, sideEnvelope = 0.0f;
    std::size_t sideDelay = 0, rearDelay = 0, backDelay = 0;
};
