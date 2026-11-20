#pragma once

#include <QImage>
#include <QWidget>
#include <QVideoFrame>
#include <QVideoSink>

class VideoDisplayWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoDisplayWidget(QWidget *parent = nullptr);
    QVideoSink *videoSink() { return &sink; }
    void setAspectMode(int mode);
    void setDeinterlaceMode(int mode);
    void setSimulationMode(int mode);
    void setReceptionMode(int mode);
    void setSparklies(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void presentFrame(const QVideoFrame &frame);

private:
    QImage processFrame(const QImage &source) const;
    QVideoSink sink;
    QImage image;
    int aspectMode = 0;
    int deinterlaceMode = 0;
    int simulationMode = 0;
    int receptionMode = 0;
    bool sparklies = false;
    quint64 frameCounter = 0;
};
