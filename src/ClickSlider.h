#pragma once

#include <QMouseEvent>
#include <QSlider>
#include <QStyle>

// Curseur qui saute directement à l'endroit cliqué (au lieu d'avancer
// par pas de page), puis laisse l'utilisateur continuer à le faire glisser.
class ClickSlider : public QSlider
{
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton
            && orientation() == Qt::Horizontal
            && width() > 0) {
            setValue(QStyle::sliderValueFromPosition(
                minimum(), maximum(),
                static_cast<int>(event->position().x()), width()));
        }

        QSlider::mousePressEvent(event);
    }
};
