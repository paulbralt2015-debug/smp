#include "SurroundOutput.h"

#include <QAudio>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
float sampleToFloat(const QAudioBuffer &buffer, qsizetype index)
{
    switch (buffer.format().sampleFormat()) {
    case QAudioFormat::UInt8:
        return (static_cast<float>(buffer.constData<quint8>()[index]) - 128.0f) / 128.0f;
    case QAudioFormat::Int16:
        return static_cast<float>(buffer.constData<qint16>()[index]) / 32768.0f;
    case QAudioFormat::Int32:
        return static_cast<float>(static_cast<double>(buffer.constData<qint32>()[index]) / 2147483648.0);
    case QAudioFormat::Float: {
        const float value = buffer.constData<float>()[index];
        return std::isfinite(value) ? value : 0.0f;
    }
    default:
        return 0.0f;
    }
}

bool supportedInputFormat(QAudioFormat::SampleFormat format)
{
    return format == QAudioFormat::UInt8 || format == QAudioFormat::Int16
        || format == QAudioFormat::Int32 || format == QAudioFormat::Float;
}
}

SurroundOutput::SurroundOutput(QObject *parent)
    : QObject(parent), proc(SampleRate), pumpTimer(this)
{
    device = QMediaDevices::defaultAudioOutput();
    pumpTimer.setInterval(5);
    connect(&pumpTimer, &QTimer::timeout, this, &SurroundOutput::pump);
    pumpTimer.start();
}

SurroundOutput::~SurroundOutput()
{
    if (sink)
        sink->stop();
}

QAudioFormat SurroundOutput::inputFormat() const
{
    QAudioFormat format;
    format.setSampleRate(SampleRate);
    format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    format.setSampleFormat(QAudioFormat::Float);
    return format;
}

QAudioFormat SurroundOutput::formatFor(SurroundProcessor::Mode mode)
{
    QAudioFormat format;
    format.setSampleRate(SampleRate);
    format.setSampleFormat(QAudioFormat::Float);
    if (mode == SurroundProcessor::Surround71)
        format.setChannelConfig(QAudioFormat::ChannelConfigSurround7Dot1);
    else if (mode == SurroundProcessor::Surround51)
        format.setChannelConfig(QAudioFormat::ChannelConfigSurround5Dot1);
    else
        format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    return format;
}

void SurroundOutput::setDevice(const QAudioDevice &newDevice)
{
    device = newDevice;
    if (active)
        rebuildSink();
}

void SurroundOutput::setRequestedMode(SurroundProcessor::Mode mode)
{
    requestedMode = mode;
    if (active)
        rebuildSink();
}

void SurroundOutput::setActive(bool enabled)
{
    if (active == enabled)
        return;
    active = enabled;
    rebuildSink();
}

void SurroundOutput::setVolume(float linearVolume)
{
    volume = std::clamp(linearVolume, 0.0f, 1.0f);
    if (sink)
        sink->setVolume(volume);
}

void SurroundOutput::rebuildSink()
{
    const bool wasReady = isReady();
    if (sink) {
        sink->stop();
        sink->deleteLater();
        sink = nullptr;
        io = nullptr;
    }
    pending.clear();
    hasReceivedBuffer = false;
    if (wasReady)
        emit outputReadyChanged(false);

    if (!active) {
        emit statusMessage("Traitement multicanal désactivé");
        return;
    }

    SurroundProcessor::Mode candidates[3] = { requestedMode, SurroundProcessor::Stereo, SurroundProcessor::Stereo };
    int candidateCount = 2;
    if (requestedMode == SurroundProcessor::Surround71) {
        candidates[1] = SurroundProcessor::Surround51;
        candidates[2] = SurroundProcessor::Stereo;
        candidateCount = 3;
    }

    const QAudioFormat::SampleFormat formats[] = {
        QAudioFormat::Float, QAudioFormat::Int16, QAudioFormat::Int32
    };

    for (int c = 0; c < candidateCount; ++c) {
        const auto mode = candidates[c];
        QAudioFormat chosen;
        bool found = false;
        for (const auto sampleFormat : formats) {
            QAudioFormat trial = formatFor(mode);
            trial.setSampleFormat(sampleFormat);
            if (!device.isNull() && device.isFormatSupported(trial)) {
                chosen = trial;
                found = true;
                break;
            }
        }
        if (!found)
            continue;

        auto *candidate = new QAudioSink(device, chosen, this);
        candidate->setBufferSize(chosen.bytesForDuration(80000));
        candidate->setVolume(volume);
        QIODevice *stream = candidate->start();
        if (!stream) {
            candidate->deleteLater();
            continue;
        }

        sink = candidate;
        io = stream;
        outputFormat = chosen;
        currentMode = mode;
        proc.setMode(mode);

        const QString modeName = mode == SurroundProcessor::Surround71 ? "7.1"
                               : mode == SurroundProcessor::Surround51 ? "5.1" : "stéréo";
        emit statusMessage(QString("Sortie %1 ouverte ; attente du flux audio").arg(modeName));
        emit outputReadyChanged(false);
        return;
    }

    emit statusMessage("Sortie DSP indisponible : lecture audio système de secours");
    emit outputReadyChanged(false);
}

void SurroundOutput::handleBuffer(const QAudioBuffer &buffer)
{
    if (!active || !sink || !io || !buffer.isValid())
        return;

    const QAudioFormat input = buffer.format();
    const int channels = input.channelCount();
    const int rate = input.sampleRate();
    const int frames = buffer.frameCount();
    if (channels < 1 || rate < 8000 || frames <= 0 || !supportedInputFormat(input.sampleFormat())) {
        emit statusMessage("Format audio source non pris en charge par le DSP");
        return;
    }

    if (!hasReceivedBuffer) {
        hasReceivedBuffer = true;
        emit outputReadyChanged(true);
        emit statusMessage(QString("DSP actif : source %1 Hz / %2 canaux, sortie %3 canaux")
            .arg(rate).arg(channels).arg(outputFormat.channelCount()));
    }

    // Conversion explicite vers stéréo float 48 kHz. Cela évite de rejeter
    // silencieusement les fichiers PCM 44,1 kHz, 5.1, entiers ou mono.
    const int outFrames = std::max(1, static_cast<int>(
        std::llround(static_cast<double>(frames) * SampleRate / rate)));
    std::vector<float> stereo(static_cast<std::size_t>(outFrames) * 2);
    auto readStereo = [&](int frame, int channel) {
        const qsizetype base = static_cast<qsizetype>(frame) * channels;
        if (channels == 1)
            return sampleToFloat(buffer, base);
        if (channel == 0)
            return sampleToFloat(buffer, base);
        if (channel == 1)
            return sampleToFloat(buffer, base + 1);
        return 0.0f;
    };

    for (int i = 0; i < outFrames; ++i) {
        const double sourcePosition = static_cast<double>(i) * rate / SampleRate;
        const int a = std::min(frames - 1, static_cast<int>(sourcePosition));
        const int b = std::min(frames - 1, a + 1);
        const float fraction = static_cast<float>(sourcePosition - a);
        for (int ch = 0; ch < 2; ++ch) {
            const float va = readStereo(a, ch);
            const float vb = readStereo(b, ch);
            stereo[static_cast<std::size_t>(i) * 2 + ch] = va + (vb - va) * fraction;
        }
    }

    proc.process(stereo.data(), outFrames, scratch);
    if (scratch.empty())
        return;

    if (outputFormat.sampleFormat() == QAudioFormat::Float) {
        pending.append(reinterpret_cast<const char *>(scratch.data()),
                       static_cast<qsizetype>(scratch.size() * sizeof(float)));
    } else if (outputFormat.sampleFormat() == QAudioFormat::Int16) {
        std::vector<qint16> pcm(scratch.size());
        for (std::size_t i = 0; i < scratch.size(); ++i)
            pcm[i] = static_cast<qint16>(std::lrint(std::clamp(scratch[i], -1.0f, 1.0f) * 32767.0f));
        pending.append(reinterpret_cast<const char *>(pcm.data()),
                       static_cast<qsizetype>(pcm.size() * sizeof(qint16)));
    } else if (outputFormat.sampleFormat() == QAudioFormat::Int32) {
        std::vector<qint32> pcm(scratch.size());
        for (std::size_t i = 0; i < scratch.size(); ++i)
            pcm[i] = static_cast<qint32>(std::llround(
                std::clamp(static_cast<double>(scratch[i]), -1.0, 1.0) * 2147483647.0));
        pending.append(reinterpret_cast<const char *>(pcm.data()),
                       static_cast<qsizetype>(pcm.size() * sizeof(qint32)));
    }

    const qint64 frameBytes = outputFormat.bytesPerFrame();
    const qint64 maximum = outputFormat.bytesForDuration(750000);
    if (frameBytes > 0 && pending.size() > maximum) {
        qint64 excess = pending.size() - maximum;
        excess -= excess % frameBytes;
        pending.remove(0, static_cast<qsizetype>(excess));
    }
    pump();
}

void SurroundOutput::pump()
{
    if (!sink || !io || pending.isEmpty() || sink->state() == QtAudio::SuspendedState)
        return;
    const qint64 frameBytes = outputFormat.bytesPerFrame();
    if (frameBytes <= 0)
        return;
    qint64 count = std::min<qint64>(sink->bytesFree(), pending.size());
    count -= count % frameBytes;
    if (count <= 0)
        return;
    const qint64 written = io->write(pending.constData(), count);
    if (written > 0)
        pending.remove(0, static_cast<qsizetype>(written));
}

void SurroundOutput::pause()
{
    if (sink)
        sink->suspend();
}

void SurroundOutput::resume()
{
    if (sink && sink->state() == QtAudio::SuspendedState)
        sink->resume();
}

void SurroundOutput::flush()
{
    pending.clear();
    proc.reset();
    hasReceivedBuffer = false;
    if (sink) {
        sink->reset();
        io = sink->start();
    }
    emit outputReadyChanged(false);
}
