#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

// Décodeur audio FFmpeg en flux : PCM float entrelacé à fréquence/canaux choisis.
class FfmpegAudioDecoder
{
public:
    FfmpegAudioDecoder();
    ~FfmpegAudioDecoder();

    FfmpegAudioDecoder(const FfmpegAudioDecoder &) = delete;
    FfmpegAudioDecoder &operator=(const FfmpegAudioDecoder &) = delete;

    bool open(const std::string &fileName, int outputRate = 48000,
              int outputChannels = 2);
    void close();
    bool readFrames(std::vector<float> &pcm, int maxFrames);
    bool seek(int64_t timestampMs);

    int sampleRate() const { return outRate; }
    int channels() const { return outChannels; }
    int64_t durationMs() const { return duration; }
    const std::string &errorString() const { return error; }

private:
    bool receiveFrame(std::vector<float> &pcm, int maxFrames);
    bool configureResampler();

    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    SwrContext *resampler = nullptr;
    int streamIndex = -1;
    int outRate = 48000;
    int outChannels = 2;
    int64_t duration = 0;
    bool draining = false;
    bool decoderEof = false;
    std::string error;
};
