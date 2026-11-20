#include "SurroundOutput.h"

#include <QAudio>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>

SurroundOutput::SurroundOutput(QObject *parent)
    : QObject(parent)
    , proc(SampleRate)
    , pumpTimer(this)
{
    device = QMediaDevices::defaultAudioOutput();

    // Le QAudioSink est alimenté en mode « push » : on vide régulièrement
    // la file d'attente dans la limite de la place libre du sink.
    pumpTimer.setInterval(8);
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

    switch (mode) {
    case SurroundProcessor::Surround71:
        format.setChannelConfig(QAudioFormat::ChannelConfigSurround7Dot1);
        break;
    case SurroundProcessor::Surround51:
        format.setChannelConfig(QAudioFormat::ChannelConfigSurround5Dot1);
        break;
    default:
        format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
        break;
    }

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
    if (sink) {
        sink->stop();
        sink->deleteLater();
        sink = nullptr;
        io = nullptr;
    }

    pending.clear();

    if (!active)
        return;

    // Essaie les configurations de canaux et plusieurs formats PCM.
    // De nombreux pilotes WASAPI refusent le float 32 bits mais acceptent
    // le PCM signé 16/32 bits : ne pas laisser le lecteur muet pour cela.
    const SurroundProcessor::Mode candidates[] = {
        requestedMode == SurroundProcessor::Surround71 ? SurroundProcessor::Surround71 : SurroundProcessor::Surround51,
        requestedMode == SurroundProcessor::Stereo ? SurroundProcessor::Stereo : SurroundProcessor::Stereo
    };
    const QAudioFormat::SampleFormat sampleFormats[] = {
        QAudioFormat::Float, QAudioFormat::Int16, QAudioFormat::Int32
    };

    for (const auto mode : candidates) {
        if (mode > requestedMode)
            continue;
        QAudioFormat format = formatFor(mode);
        bool supported = false;
        if (!device.isNull()) {
            for (const auto sampleFormat : sampleFormats) {
                format.setSampleFormat(sampleFormat);
                if (device.isFormatSupported(format)) {
                    supported = true;
                    break;
                }
            }
        }
        if (!supported)
            continue;

        sink = new QAudioSink(device, format, this);
        sink->setBufferSize(format.bytesForDuration(100000)); // 100 ms
        sink->setVolume(volume);

        io = sink->start();

        if (!io) {
            sink->deleteLater();
            sink = nullptr;
            continue;
        }

        outputFormat = format;
        currentMode = mode;
        proc.setMode(mode);

        if (mode == requestedMode) {
            emit statusMessage(
                QString("Upmix %1 actif").arg(
                    mode == SurroundProcessor::Surround71 ? "7.1" :
                    mode == SurroundProcessor::Surround51 ? "5.1" : "stéréo"));
        } else {
            emit statusMessage(
                QString("Le périphérique ne supporte pas ce mode : repli sur %1")
                    .arg(mode == SurroundProcessor::Surround51 ? "5.1" : "la stéréo"));
        }

        return;
    }

    emit statusMessage("Aucun mode audio compatible avec ce périphérique");
}

void SurroundOutput::handleBuffer(const QAudioBuffer &buffer)
{
    if (!active || !sink || !io || !buffer.isValid())
        return;

    const QAudioFormat format = buffer.format();

    if (format.channelCount() != 2
        || format.sampleFormat() != QAudioFormat::Float
        || format.sampleRate() != SampleRate)
        return;

    proc.process(buffer.constData<float>(), buffer.frameCount(), scratch);

    if (scratch.empty())
        return;

    if (outputFormat.sampleFormat() == QAudioFormat::Float) {
        pending.append(reinterpret_cast<const char *>(scratch.data()),
                       static_cast<qsizetype>(scratch.size() * sizeof(float)));
    } else if (outputFormat.sampleFormat() == QAudioFormat::Int16) {
        std::vector<qint16> pcm(scratch.size());
        for (std::size_t i = 0; i < scratch.size(); ++i) {
            const float v = std::clamp(scratch[i], -1.0f, 1.0f);
            pcm[i] = static_cast<qint16>(std::lrint(v * 32767.0f));
        }
        pending.append(reinterpret_cast<const char *>(pcm.data()),
                       static_cast<qsizetype>(pcm.size() * sizeof(qint16)));
    } else if (outputFormat.sampleFormat() == QAudioFormat::Int32) {
        std::vector<qint32> pcm(scratch.size());
        for (std::size_t i = 0; i < scratch.size(); ++i) {
            const double v = std::clamp(static_cast<double>(scratch[i]), -1.0, 1.0);
            pcm[i] = static_cast<qint32>(std::llround(v * 2147483647.0));
        }
        pending.append(reinterpret_cast<const char *>(pcm.data()),
                       static_cast<qsizetype>(pcm.size() * sizeof(qint32)));
    } else {
        emit statusMessage("Format PCM de sortie non pris en charge");
        return;
    }

    // Garde-fou : jamais plus d'1 seconde en attente.
    const qint64 frameBytes = outputFormat.bytesPerFrame();
    const qint64 maxPending = outputFormat.bytesForDuration(1000000);

    if (pending.size() > maxPending) {
        qint64 excess = pending.size() - maxPending;
        excess -= excess % frameBytes;
        pending.remove(0, static_cast<qsizetype>(excess));
    }

    pump();
}

void SurroundOutput::pump()
{
    if (!sink || !io || pending.isEmpty())
        return;

    if (sink->state() == QtAudio::SuspendedState)
        return;

    const qint64 frameBytes = outputFormat.bytesPerFrame();

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

    if (sink) {
        sink->reset();
        io = sink->start();
    }
}
