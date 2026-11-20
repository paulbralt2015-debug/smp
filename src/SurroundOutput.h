#pragma once

#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include <vector>

#include "SurroundProcessor.h"

class QAudioSink;
class QIODevice;

// Reçoit l'audio décodé (stéréo float 48 kHz) du lecteur, le passe dans
// SurroundProcessor, puis le joue en 5.1 / 7.1 sur un QAudioSink.
// Si le périphérique ne supporte pas le nombre de canaux demandé,
// on retombe sur 5.1 puis sur la stéréo.
class SurroundOutput : public QObject
{
    Q_OBJECT

public:
    static constexpr int SampleRate = 48000;

    explicit SurroundOutput(QObject *parent = nullptr);
    ~SurroundOutput() override;

    // Format à demander à QAudioBufferOutput.
    QAudioFormat inputFormat() const;

    void setDevice(const QAudioDevice &newDevice);
    void setRequestedMode(SurroundProcessor::Mode mode);
    void setActive(bool enabled);
    void setVolume(float linearVolume);

    SurroundProcessor::Mode activeMode() const { return currentMode; }
    SurroundProcessor &processor() { return proc; }

public slots:
    void handleBuffer(const QAudioBuffer &buffer);
    void pause();
    void resume();
    void flush();

signals:
    void statusMessage(const QString &message);

private:
    static QAudioFormat formatFor(SurroundProcessor::Mode mode);

    void rebuildSink();
    void pump();

    SurroundProcessor proc;
    SurroundProcessor::Mode requestedMode = SurroundProcessor::Surround51;
    SurroundProcessor::Mode currentMode = SurroundProcessor::Stereo;

    QAudioDevice device;
    QAudioFormat outputFormat;
    QAudioSink *sink = nullptr;
    QIODevice *io = nullptr;

    QByteArray pending;
    std::vector<float> scratch;
    QTimer pumpTimer;

    bool active = false;
    float volume = 0.8f;
};
