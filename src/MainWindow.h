#pragma once

#include <QAudioDevice>
#include <QAudioOutput>
#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSettings>
#include <QString>
#include <QTimer>

class QAudioBufferOutput;
class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSlider;
class QVideoWidget;
class QWidget;
class SurroundOutput;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    // Ouvre et lance la lecture d'un fichier local.
    void openPath(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void openFile();
    void playPause();
    void stopPlayback();
    void seekTo(qint64 position);
    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void refreshAudioDevices();
    void selectAudioDevice(int index);
    void refreshTracks();
    void applyAudioMode();
    void applyMatrixMode();
    void applyProfile();
    void applyLevels();
    void applyVolume();
    void toggleMute();
    void toggleFullscreen();
    void showControlsTemporarily();
    void saveResumePosition();
    QString resumeKey(const QString &path) const;
    QString formatTime(qint64 milliseconds) const;

    QSettings settings;

    QMediaPlayer *player = nullptr;
    QAudioOutput *audioOutput = nullptr;
    QAudioBufferOutput *bufferOutput = nullptr;
    SurroundOutput *surround = nullptr;
    QVideoWidget *videoWidget = nullptr;
    QWidget *controlsPanel = nullptr;
    QTimer *hideControlsTimer = nullptr;

    QPushButton *openButton = nullptr;
    QPushButton *playButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *fullscreenButton = nullptr;

    QSlider *positionSlider = nullptr;
    QSlider *volumeSlider = nullptr;
    QSlider *surroundSlider = nullptr;
    QSlider *centerSlider = nullptr;
    QSlider *lfeSlider = nullptr;

    QComboBox *audioDeviceBox = nullptr;
    QComboBox *modeBox = nullptr;
    QComboBox *matrixBox = nullptr;
    QComboBox *profileBox = nullptr;
    QComboBox *audioTrackBox = nullptr;
    QComboBox *subtitleBox = nullptr;

    QLabel *statusLabel = nullptr;
    QLabel *timeLabel = nullptr;

    bool sliderPressed = false;
    bool muted = false;
    qint64 pendingResume = 0;
    QString currentPath;
    QByteArray currentDeviceId;
    QList<QAudioDevice> audioDevices;
};
