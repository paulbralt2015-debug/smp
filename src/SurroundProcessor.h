#pragma once

#include <cstddef>
#include <vector>

// Upmix stéréo -> 5.1 / 7.1 par matriçage, façon Dolby Surround / Pro Logic :
//  - centre   : information commune G/D (mid), retirée en partie des fronts ;
//  - surround : information différentielle G/D (side), filtrée passe-bas
//               (~7 kHz) et retardée (10 à 22 ms) pour l'effet d'espace ;
//  - LFE      : mid filtré par un Linkwitz-Riley d'ordre 4 à 120 Hz ;
//  - limiteur doux en sortie au lieu d'un écrêtage brutal.
//
// Cette classe n'est pas thread-safe : utiliser un seul thread à la fois.
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

    float surroundLevel() const { return surroundGain; }
    float centerLevel() const { return centerGain; }
    float lfeLevel() const { return lfeGain; }

    int outputChannels() const;

    // Entrée : audio stéréo intercalé en float [-1, 1].
    // Sortie : audio intercalé en float [-1, 1], ordre des canaux :
    //   5.1 : FL, FR, FC, LFE, BL, BR
    //   7.1 : FL, FR, FC, LFE, BL, BR, SL, SR
    // « output » est vidé si l'entrée est invalide.
    void process(const float *input, int frames, std::vector<float> &output);

    void reset();

private:
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
        float previousInput = 0.0f;
        float previousOutput = 0.0f;
        float process(float input)
        {
            const float output = -coefficient * input + previousInput
                               + coefficient * previousOutput;
            previousInput = input;
            previousOutput = output;
            return output;
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

    float surroundGain = 0.75f;
    float centerGain = 1.0f;
    float lfeGain = 0.8f;
    float inputGain = 1.0f;
    float normalizerEnvelope = 0.0f;
    bool voiceNormalization = false;

    Biquad lfeLow1;
    Biquad lfeLow2;
    Biquad surroundLow;

    DelayLine delayA;
    DelayLine delayB;
    AllPass surroundAllPassA;
    AllPass surroundAllPassB;
    float midEnvelope = 0.0f;
    float sideEnvelope = 0.0f;

    std::size_t sideDelay = 0;
    std::size_t rearDelay = 0;
    std::size_t backDelay = 0;
};
