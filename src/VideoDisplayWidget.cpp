#include "VideoDisplayWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

VideoDisplayWidget::VideoDisplayWidget(QWidget *parent) : QWidget(parent), sink(this)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
    connect(&sink, &QVideoSink::videoFrameChanged, this, &VideoDisplayWidget::presentFrame);
}

void VideoDisplayWidget::setAspectMode(int mode) { aspectMode = mode; update(); }
void VideoDisplayWidget::setDeinterlaceMode(int mode) { deinterlaceMode = mode; update(); }
void VideoDisplayWidget::setSimulationMode(int mode) { simulationMode = mode; update(); }
void VideoDisplayWidget::setReceptionMode(int mode) { receptionMode = mode; update(); }
void VideoDisplayWidget::setSparklies(bool enabled) { sparklies = enabled; update(); }

void VideoDisplayWidget::presentFrame(const QVideoFrame &frame)
{
    QVideoFrame copy(frame);
    if (!copy.map(QVideoFrame::ReadOnly))
        return;
    const QImage decoded = copy.toImage();
    copy.unmap();
    if (decoded.isNull())
        return;
    image = processFrame(decoded);
    ++frameCounter;
    update();
}

QImage VideoDisplayWidget::processFrame(const QImage &source) const
{
    QImage out = source.convertToFormat(QImage::Format_RGB32);
    if (deinterlaceMode != 0) {
        // Bob / blend deinterlacing approximation on the decoded frame.
        QImage filtered(out.size(), out.format());
        for (int y = 0; y < out.height(); ++y) {
            const int a = (y / 2) * 2;
            const int b = std::min(a + 2, out.height() - 1);
            for (int x = 0; x < out.width(); ++x) {
                const QColor ca = out.pixelColor(x, a);
                const QColor cb = out.pixelColor(x, b);
                filtered.setPixelColor(x, y, deinterlaceMode == 1 ? ca :
                    QColor((ca.red()+cb.red())/2, (ca.green()+cb.green())/2,
                           (ca.blue()+cb.blue())/2));
            }
        }
        out = std::move(filtered);
    }

    if (simulationMode != 0 || receptionMode != 0 || sparklies) {
        const int w = out.width(), h = out.height();
        const int step = (simulationMode == 1 || receptionMode != 0) ? 2 : 4;
        for (int y = 0; y < h; y += step) {
            QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; x += step) {
                QColor c = QColor::fromRgb(line[x]);
                int noise = 0;
                if (simulationMode == 1) noise = int(QRandomGenerator::global()->bounded(19)) - 9;
                if (simulationMode == 2) noise = int(QRandomGenerator::global()->bounded(11)) - 5;
                if (simulationMode == 3) { c.setRed(std::min(255, c.red()+3)); c.setBlue(std::max(0,c.blue()-2)); }
                if (simulationMode == 4) noise = int(QRandomGenerator::global()->bounded(15)) - 7;
                if (receptionMode == 1) noise += int(QRandomGenerator::global()->bounded(13)) - 6;
                if (receptionMode == 2) noise += int(QRandomGenerator::global()->bounded(9)) - 4;
                c.setRed(std::clamp(c.red()+noise,0,255));
                c.setGreen(std::clamp(c.green()+noise,0,255));
                c.setBlue(std::clamp(c.blue()+noise,0,255));
                if (simulationMode == 1 && (y % 8) < 2) {
                    c.setRed(c.red()*9/10); c.setGreen(c.green()*9/10); c.setBlue(c.blue()*9/10);
                }
                line[x] = c.rgb();
            }
        }
        if (simulationMode == 1 || simulationMode == 4 || receptionMode == 1) {
            QPainter p(&out);
            p.setOpacity(0.10);
            p.drawImage(2, 0, out);
        }
        if (sparklies && frameCounter % 3 == 0) {
            QPainter p(&out);
            for (int i=0; i<std::max(1,w*h/180000); ++i) {
                const int x=QRandomGenerator::global()->bounded(w);
                const int y=QRandomGenerator::global()->bounded(h);
                p.fillRect(x,y,2,2,Qt::white);
            }
        }
    }
    return out;
}

void VideoDisplayWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (image.isNull()) return;
    QSize target = image.size();
    if (aspectMode == 1) target = size();
    else if (aspectMode == 2) target.scale(size(), Qt::KeepAspectRatio);
    else target.scale(size(), Qt::KeepAspectRatio);
    const QRect area((width()-target.width())/2, (height()-target.height())/2,
                     target.width(), target.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(area, image);
}
