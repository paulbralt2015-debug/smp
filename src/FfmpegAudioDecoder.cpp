#include "FfmpegAudioDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

#include <algorithm>

namespace {
std::string ffError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}
}

FfmpegAudioDecoder::FfmpegAudioDecoder() = default;
FfmpegAudioDecoder::~FfmpegAudioDecoder() { close(); }

void FfmpegAudioDecoder::close()
{
    swr_free(&resampler);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    streamIndex = -1;
    draining = false;
    decoderEof = false;
    duration = 0;
}

bool FfmpegAudioDecoder::open(const std::string &fileName, int outputRate,
                              int outputChannels)
{
    close();
    error.clear();
    if (outputRate <= 0 || outputChannels < 1 || outputChannels > 8) {
        error = "Format PCM de sortie invalide";
        return false;
    }
    outRate = outputRate;
    outChannels = outputChannels;

    int rc = avformat_open_input(&format, fileName.c_str(), nullptr, nullptr);
    if (rc < 0) { error = "Ouverture FFmpeg : " + ffError(rc); close(); return false; }
    rc = avformat_find_stream_info(format, nullptr);
    if (rc < 0) { error = "Analyse du fichier : " + ffError(rc); close(); return false; }

    const AVCodec *decoder = nullptr;
    streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (streamIndex < 0) { error = "Aucune piste audio : " + ffError(streamIndex); close(); return false; }

    codec = avcodec_alloc_context3(decoder);
    if (!codec) { error = "Allocation du décodeur impossible"; close(); return false; }
    rc = avcodec_parameters_to_context(codec, format->streams[streamIndex]->codecpar);
    if (rc < 0) { error = "Paramètres codec : " + ffError(rc); close(); return false; }
    rc = avcodec_open2(codec, decoder, nullptr);
    if (rc < 0) { error = "Initialisation codec : " + ffError(rc); close(); return false; }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) { error = "Allocation des buffers FFmpeg impossible"; close(); return false; }

    const auto stream = format->streams[streamIndex];
    duration = stream->duration == AV_NOPTS_VALUE ? 0 :
        av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1000});
    if (!configureResampler()) { close(); return false; }
    return true;
}

bool FfmpegAudioDecoder::configureResampler()
{
    AVChannelLayout outputLayout = {};
    av_channel_layout_default(&outputLayout, outChannels);
    const int rc = swr_alloc_set_opts2(&resampler, &outputLayout, AV_SAMPLE_FMT_FLT,
        outRate, &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outputLayout);
    if (rc < 0 || !resampler) {
        error = "Configuration du rééchantillonneur : " + ffError(rc);
        return false;
    }
    const int initResult = swr_init(resampler);
    if (initResult < 0) {
        error = "Initialisation du rééchantillonneur : " + ffError(initResult);
        return false;
    }
    return true;
}

bool FfmpegAudioDecoder::receiveFrame(std::vector<float> &pcm, int maxFrames)
{
    const int rc = avcodec_receive_frame(codec, frame);
    if (rc == AVERROR_EOF) { decoderEof = true; return false; }
    if (rc == AVERROR(EAGAIN)) return false;
    if (rc < 0) { error = "Décodage audio : " + ffError(rc); return false; }

    const int capacity = static_cast<int>(av_rescale_rnd(
        swr_get_delay(resampler, codec->sample_rate) + frame->nb_samples,
        outRate, codec->sample_rate, AV_ROUND_UP));
    const int boundedCapacity = std::min(maxFrames, std::max(0, capacity));
    if (boundedCapacity == 0) { av_frame_unref(frame); return true; }

    pcm.resize(static_cast<size_t>(boundedCapacity) * outChannels);
    uint8_t *dst[] = { reinterpret_cast<uint8_t *>(pcm.data()) };
    const uint8_t **src = const_cast<const uint8_t **>(frame->extended_data);
    const int count = swr_convert(resampler, dst, boundedCapacity, src, frame->nb_samples);
    av_frame_unref(frame);
    if (count < 0) {
        error = "Conversion PCM : " + ffError(count);
        pcm.clear();
        return false;
    }
    pcm.resize(static_cast<size_t>(count) * outChannels);
    return true;
}

bool FfmpegAudioDecoder::readFrames(std::vector<float> &pcm, int maxFrames)
{
    pcm.clear();
    if (!codec || maxFrames <= 0 || decoderEof) return false;

    while (pcm.empty() && !decoderEof) {
        if (receiveFrame(pcm, maxFrames)) {
            if (!pcm.empty()) return true;
        }
        if (!error.empty() || decoderEof) return false;

        int rc = av_read_frame(format, packet);
        if (rc < 0) {
            if (!draining) {
                draining = true;
                rc = avcodec_send_packet(codec, nullptr);
                if (rc < 0 && rc != AVERROR_EOF) {
                    error = "Fin du flux : " + ffError(rc);
                    return false;
                }
                continue;
            }
            // Demande au décodeur de vider les dernières trames.
            continue;
        }

        if (packet->stream_index == streamIndex) {
            rc = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (rc < 0 && rc != AVERROR(EAGAIN)) {
                error = "Envoi du paquet : " + ffError(rc);
                return false;
            }
        } else {
            av_packet_unref(packet);
        }
    }
    return !pcm.empty();
}

bool FfmpegAudioDecoder::seek(int64_t timestampMs)
{
    if (!format || streamIndex < 0) return false;
    const int64_t timestamp = av_rescale_q(timestampMs, AVRational{1, 1000},
        format->streams[streamIndex]->time_base);
    const int rc = av_seek_frame(format, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) { error = "Recherche audio : " + ffError(rc); return false; }
    avcodec_flush_buffers(codec);
    swr_close(resampler);
    const int initResult = swr_init(resampler);
    if (initResult < 0) { error = "Réinitialisation du rééchantillonneur : " + ffError(initResult); return false; }
    draining = false;
    decoderEof = false;
    error.clear();
    return true;
}
