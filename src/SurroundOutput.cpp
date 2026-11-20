#include "SurroundOutput.h"

#include <QAudio>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>

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

    // On essaie le mode demandé, puis on descend : 7.1 -> 5.1 -> stéréo.
    const SurroundProcessor::Mode candidates[] = {
        SurroundProcessor::Surround71,
        SurroundProcessor::Surround51,
        SurroundProcessor::Stereo
    };

    for (const auto mode : candidates) {
        if (mode > requestedMode)
            continue;

        const QAudioFormat format = formatFor(mode);

        if (device.isNull() || !device.isFormatSupported(format))
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

    pending.append(reinterpret_cast<const char *>(scratch.data()),
                   static_cast<qsizetype>(scratch.size() * sizeof(float)));

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
