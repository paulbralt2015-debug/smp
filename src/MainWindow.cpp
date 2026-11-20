#include "MainWindow.h"

#include "ClickSlider.h"
#include "SurroundOutput.h"
#include "VideoDisplayWidget.h"

#include <QAudio>
#include <QAudioBufferOutput>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMediaMetaData>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

#include <QWidget>

#include <algorithm>

namespace {

QString trackLabel(const QMediaMetaData &meta, int index, const QString &fallback)
{
    QStringList parts;

    const QString title = meta.stringValue(QMediaMetaData::Title);
    if (!title.isEmpty())
        parts << title;

    const QVariant languageValue = meta.value(QMediaMetaData::Language);
    if (languageValue.isValid()) {
        const auto language = languageValue.value<QLocale::Language>();
        if (language != QLocale::AnyLanguage)
            parts << QLocale::languageToString(language);
    }

    if (parts.isEmpty())
        return QString("%1 %2").arg(fallback).arg(index + 1);

    return QString("%1 %2 - %3")
        .arg(fallback)
        .arg(index + 1)
        .arg(parts.join(" / "));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Cinema Surround Player");
    resize(1100, 720);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);

    // ---- Lecture -------------------------------------------------------
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    player->setAudioOutput(audioOutput);

    surround = new SurroundOutput(this);
    bufferOutput = new QAudioBufferOutput(surround->inputFormat(), this);

    connect(bufferOutput, &QAudioBufferOutput::audioBufferReceived,
            surround, &SurroundOutput::handleBuffer);

    videoWidget = new VideoDisplayWidget(this);
    videoWidget->setMouseTracking(true);
    videoWidget->installEventFilter(this);
    player->setVideoSink(videoWidget->videoSink());

    // ---- Widgets -------------------------------------------------------
    openButton = new QPushButton("Ouvrir");
    playButton = new QPushButton("Lecture");
    stopButton = new QPushButton("Stop");
    fullscreenButton = new QPushButton("Plein écran");

    positionSlider = new ClickSlider(Qt::Horizontal);
    positionSlider->setRange(0, 0);

    volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setMaximumWidth(150);

    amplificationSlider = new QSlider(Qt::Horizontal);
    amplificationSlider->setRange(100, 200);
    amplificationSlider->setValue(settings.value("amplification", 100).toInt());
    amplificationSlider->setMaximumWidth(110);
    voiceNormalizeBox = new QCheckBox("Normaliser les voix / le son");
    voiceNormalizeBox->setChecked(settings.value("voiceNormalization", false).toBool());

    aspectBox = new QComboBox();
    aspectBox->addItem("Format original", 0);
    aspectBox->addItem("Étirer à la fenêtre", 1);
    aspectBox->addItem("4:3", 2);
    aspectBox->addItem("16:9", 3);
    aspectBox->addItem("21:9", 4);
    aspectBox->setCurrentIndex(settings.value("aspectMode", 0).toInt());

    deinterlaceBox = new QComboBox();
    deinterlaceBox->addItem("Désentrelacement désactivé", 0);
    deinterlaceBox->addItem("Bob (champ pair)", 1);
    deinterlaceBox->addItem("Blend (mélange des champs)", 2);
    deinterlaceBox->setCurrentIndex(settings.value("deinterlaceMode", 0).toInt());
    simulationBox = new QComboBox();
    simulationBox->addItem("Image normale", 0);
    simulationBox->addItem("VHS", 1);
    simulationBox->addItem("LaserDisc", 2);
    simulationBox->addItem("DVD", 3);
    simulationBox->addItem("Betamax", 4);
    simulationBox->setCurrentIndex(settings.value("simulationMode", 0).toInt());
    receptionBox = new QComboBox();
    receptionBox->addItem("Réception propre", 0);
    receptionBox->addItem("Satellite analogique", 1);
    receptionBox->addItem("Terrestre analogique", 2);
    receptionBox->addItem("Satellite numérique", 3);
    receptionBox->addItem("TNT / terrestre numérique", 4);
    receptionBox->setCurrentIndex(settings.value("receptionMode", 0).toInt());
    sparkliesBox = new QCheckBox("Ajouter des sparklies");
    sparkliesBox->setChecked(settings.value("sparklies", false).toBool());

    audioModeBox = new QComboBox();
    audioModeBox->addItem("Audio par défaut", 0);
    audioModeBox->addItem("Stéréo 2.0", 1);
    audioModeBox->addItem("Dolby Surround Pro Logic classique", 2);
    audioModeBox->addItem("Pro Logic II - Movie", 3);
    audioModeBox->addItem("Pro Logic II - Music", 4);
    audioModeBox->addItem("Passthrough (bitstream)", 5);
    audioModeBox->setCurrentIndex(settings.value("audioMode", 0).toInt());

    audioApiBox = new QComboBox();
    audioApiBox->addItem("WASAPI (Windows / Qt)", 0);
    audioApiBox->addItem("Sortie audio système", 1);
    audioApiBox->setCurrentIndex(settings.value("audioApi", 0).toInt());

    modeBox = new QComboBox();
    modeBox->addItem("Original (sans upmix)");
    modeBox->addItem("Upmix 5.1");
    modeBox->addItem("Upmix 7.1");

    matrixBox = new QComboBox();
    matrixBox->addItem("Dolby Surround (matriciel)", int(SurroundProcessor::DolbySurround));
    matrixBox->addItem("Pro Logic I (matriciel)", int(SurroundProcessor::ProLogic));
    matrixBox->addItem("Pro Logic II (matriciel)", int(SurroundProcessor::ProLogicII));
    matrixBox->addItem("Pro Logic IIx (matriciel)", int(SurroundProcessor::ProLogicIIx));
    matrixBox->setCurrentIndex(2);

    profileBox = new QComboBox();
    profileBox->addItem("Standard", int(SurroundProcessor::Standard));
    profileBox->addItem("Nuit", int(SurroundProcessor::Night));
    profileBox->addItem("Jeu", int(SurroundProcessor::Game));
    profileBox->addItem("Film", int(SurroundProcessor::Movie));
    profileBox->addItem("Musique", int(SurroundProcessor::Music));
    profileBox->setCurrentIndex(3);

    surroundSlider = new QSlider(Qt::Horizontal);
    centerSlider = new QSlider(Qt::Horizontal);
    lfeSlider = new QSlider(Qt::Horizontal);

    for (QSlider *slider : {surroundSlider, centerSlider, lfeSlider}) {
        slider->setRange(0, 150);
        slider->setMaximumWidth(120);
    }

    audioDeviceBox = new QComboBox();
    audioDeviceBox->setMinimumWidth(220);

    audioTrackBox = new QComboBox();
    audioTrackBox->setMinimumWidth(180);

    subtitleBox = new QComboBox();
    subtitleBox->setMinimumWidth(180);

    statusLabel = new QLabel("Prêt");
    timeLabel = new QLabel("00:00 / 00:00");

    // ---- Réglages mémorisés (avant de connecter les signaux) -----------
    volumeSlider->setValue(settings.value("volume", 80).toInt());
    modeBox->setCurrentIndex(
        std::clamp(settings.value("mode", 0).toInt(), 0, 2));
    surroundSlider->setValue(settings.value("surroundLevel", 75).toInt());
    centerSlider->setValue(settings.value("centerLevel", 100).toInt());
    lfeSlider->setValue(settings.value("lfeLevel", 80).toInt());
    currentDeviceId = settings.value("audioDevice").toByteArray();

    if (settings.contains("geometry"))
        restoreGeometry(settings.value("geometry").toByteArray());

    // ---- Mise en page --------------------------------------------------
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    controlsPanel = new QWidget(central);
    auto *panelLayout = new QVBoxLayout(controlsPanel);

    auto *controlLayout = new QHBoxLayout();
    controlLayout->addWidget(openButton);
    controlLayout->addWidget(playButton);
    controlLayout->addWidget(stopButton);
    controlLayout->addWidget(fullscreenButton);
    controlLayout->addStretch();

    auto *audioLayout = new QHBoxLayout();
    audioLayout->addWidget(new QLabel("Sortie audio :"));
    audioLayout->addWidget(audioDeviceBox);
    audioLayout->addSpacing(15);
    audioLayout->addWidget(new QLabel("Volume :"));
    audioLayout->addWidget(volumeSlider);
    audioLayout->addSpacing(12);
    audioLayout->addWidget(new QLabel("Amplification :"));
    audioLayout->addWidget(amplificationSlider);
    audioLayout->addWidget(voiceNormalizeBox);
    audioLayout->addStretch();

    auto *videoLayout = new QHBoxLayout();
    videoLayout->addWidget(new QLabel("Format vidéo :"));
    videoLayout->addWidget(aspectBox);
    videoLayout->addWidget(new QLabel("Désentrelacement :"));
    videoLayout->addWidget(deinterlaceBox);
    videoLayout->addWidget(new QLabel("Support :"));
    videoLayout->addWidget(simulationBox);
    videoLayout->addWidget(new QLabel("Réception :"));
    videoLayout->addWidget(receptionBox);
    videoLayout->addWidget(sparkliesBox);
    videoLayout->addStretch();

    auto *surroundLayout = new QHBoxLayout();
    surroundLayout->addWidget(new QLabel("Mode audio :"));
    surroundLayout->addWidget(audioModeBox);
    surroundLayout->addWidget(new QLabel("API :"));
    surroundLayout->addWidget(audioApiBox);
    surroundLayout->addWidget(new QLabel("Canaux :"));
    surroundLayout->addWidget(modeBox);
    surroundLayout->addWidget(new QLabel("Matrice :"));
    surroundLayout->addWidget(matrixBox);
    surroundLayout->addWidget(new QLabel("Profil :"));
    surroundLayout->addWidget(profileBox);
    surroundLayout->addSpacing(15);
    surroundLayout->addWidget(new QLabel("Surround :"));
    surroundLayout->addWidget(surroundSlider);
    surroundLayout->addWidget(new QLabel("Centre :"));
    surroundLayout->addWidget(centerSlider);
    surroundLayout->addWidget(new QLabel("LFE :"));
    surroundLayout->addWidget(lfeSlider);
    surroundLayout->addStretch();

    auto *trackLayout = new QHBoxLayout();
    trackLayout->addWidget(new QLabel("Piste audio :"));
    trackLayout->addWidget(audioTrackBox);
    trackLayout->addSpacing(15);
    trackLayout->addWidget(new QLabel("Sous-titres :"));
    trackLayout->addWidget(subtitleBox);
    trackLayout->addStretch();

    panelLayout->addWidget(positionSlider);
    panelLayout->addWidget(timeLabel);
    panelLayout->addLayout(controlLayout);
    panelLayout->addLayout(audioLayout);
    panelLayout->addLayout(videoLayout);
    panelLayout->addLayout(surroundLayout);
    panelLayout->addLayout(trackLayout);
    panelLayout->addWidget(statusLabel);

    mainLayout->addWidget(videoWidget, 1);
    mainLayout->addWidget(controlsPanel);

    setCentralWidget(central);

    // Les raccourcis clavier doivent toujours arriver à la fenêtre,
    // pas être capturés par un bouton ou un curseur qui a le focus.
    for (QWidget *child : controlsPanel->findChildren<QWidget *>())
        child->setFocusPolicy(Qt::NoFocus);

    hideControlsTimer = new QTimer(this);
    hideControlsTimer->setSingleShot(true);
    hideControlsTimer->setInterval(3000);

    connect(hideControlsTimer, &QTimer::timeout, this, [this]() {
        if (isFullScreen())
            controlsPanel->hide();
    });

    // ---- Connexions ----------------------------------------------------
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(playButton, &QPushButton::clicked, this, &MainWindow::playPause);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::stopPlayback);
    connect(fullscreenButton, &QPushButton::clicked,
            this, &MainWindow::toggleFullscreen);

    connect(volumeSlider, &QSlider::valueChanged,
            this, [this](int) { applyVolume(); });
    connect(amplificationSlider, &QSlider::valueChanged,
            this, [this](int) { applyAudioEnhancements(); });
    connect(voiceNormalizeBox, &QCheckBox::toggled,
            this, [this](bool) { applyAudioEnhancements(); });
    connect(aspectBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyVideoAspect(); });
    connect(deinterlaceBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyVideoEffects(); });
    connect(simulationBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyVideoEffects(); });
    connect(receptionBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyVideoEffects(); });
    connect(sparkliesBox, &QCheckBox::toggled,
            this, [this](bool) { applyVideoEffects(); });

    connect(audioModeBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyAudioRouting(); });
    connect(audioApiBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyAudioRouting(); });

    connect(modeBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyAudioMode(); });

    connect(matrixBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyMatrixMode(); });
    connect(profileBox, &QComboBox::currentIndexChanged,
            this, [this](int) { applyProfile(); });

    for (QSlider *slider : {surroundSlider, centerSlider, lfeSlider}) {
        connect(slider, &QSlider::valueChanged,
                this, [this](int) { applyLevels(); });
    }

    connect(surround, &SurroundOutput::statusMessage,
            statusLabel, &QLabel::setText);

    connect(surround, &SurroundOutput::outputReadyChanged,
            this, [this](bool) { applyVolume(); });

    connect(positionSlider, &QSlider::sliderPressed,
            this, [this]() { sliderPressed = true; });

    connect(positionSlider, &QSlider::sliderReleased,
            this, [this]() {
        seekTo(positionSlider->value());
        sliderPressed = false;
    });

    connect(player, &QMediaPlayer::positionChanged,
            this, &MainWindow::updatePosition);

    connect(player, &QMediaPlayer::durationChanged,
            this, &MainWindow::updateDuration);

    connect(player, &QMediaPlayer::tracksChanged,
            this, &MainWindow::refreshTracks);

    connect(player, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState) {
            playButton->setText("Pause");
            surround->resume();
        } else {
            playButton->setText("Lecture");

            if (state == QMediaPlayer::PausedState)
                surround->pause();
            else
                surround->flush();
        }
    });

    connect(player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia && pendingResume > 0) {
            player->setPosition(pendingResume);
            statusLabel->setText(
                "Reprise à " + formatTime(pendingResume));
            pendingResume = 0;
        }
        else if (status == QMediaPlayer::EndOfMedia) {
            playButton->setText("Lecture");
            statusLabel->setText("Lecture terminée");

            if (!currentPath.isEmpty())
                settings.remove(resumeKey(currentPath));
        }
    });

    connect(player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error error, const QString &message) {
        Q_UNUSED(error);
        statusLabel->setText("Erreur : " + message);
    });

    connect(audioDeviceBox, &QComboBox::currentIndexChanged,
            this, &MainWindow::selectAudioDevice);

    connect(audioTrackBox, &QComboBox::currentIndexChanged,
            this, [this](int index) {
        if (index >= 0) {
            player->setActiveAudioTrack(index);
            surround->flush();
        }
    });

    connect(subtitleBox, &QComboBox::currentIndexChanged,
            this, [this](int index) {
        if (index >= 0)
            player->setActiveSubtitleTrack(index - 1); // 0 = aucun
    });

    auto *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged,
            this, [this]() { refreshAudioDevices(); });

    refreshAudioDevices();
    applyLevels();
    applyAudioEnhancements();
    applyVideoAspect();
    applyVideoEffects();
    applyMatrixMode();
    applyProfile();
    applyAudioMode();
    applyAudioRouting();
}

// ---------------------------------------------------------------------------
// Ouverture / lecture
// ---------------------------------------------------------------------------

void MainWindow::openFile()
{
    const QString filename = QFileDialog::getOpenFileName(
        this,
        "Ouvrir une vidéo",
        settings.value("lastDir").toString(),
        "Fichiers vidéo (*.mp4 *.mkv *.avi *.mov *.wmv *.m4v *.webm *.ts *.m2ts);;"
        "Tous les fichiers (*.*)"
    );

    if (!filename.isEmpty())
        openPath(filename);
}

void MainWindow::openPath(const QString &path)
{
    const QFileInfo info(path);

    if (!info.exists() || !info.isFile()) {
        statusLabel->setText("Fichier introuvable : " + path);
        return;
    }

    saveResumePosition();

    player->stop();
    surround->flush();

    currentPath = info.absoluteFilePath();
    settings.setValue("lastDir", info.absolutePath());
    pendingResume = settings.value(resumeKey(currentPath), 0).toLongLong();

    player->setSource(QUrl::fromLocalFile(currentPath));

    setWindowTitle("Cinema Surround Player - " + info.fileName());
    statusLabel->setText("Fichier chargé");

    player->play();
}

void MainWindow::playPause()
{
    if (player->source().isEmpty()) {
        openFile();
        return;
    }

    if (player->playbackState() == QMediaPlayer::PlayingState) {
        player->pause();
        statusLabel->setText("Pause");
    } else {
        player->play();
        statusLabel->setText("Lecture");
    }
}

void MainWindow::stopPlayback()
{
    saveResumePosition();
    player->stop();
    player->setPosition(0);
    statusLabel->setText("Arrêt");
}

void MainWindow::seekTo(qint64 position)
{
    if (player->source().isEmpty())
        return;

    const qint64 upper = player->duration() > 0 ? player->duration() : position;
    position = std::clamp<qint64>(position, 0, upper);

    player->setPosition(position);
    surround->flush();
}

void MainWindow::updatePosition(qint64 position)
{
    if (!sliderPressed)
        positionSlider->setValue(static_cast<int>(position));

    timeLabel->setText(
        formatTime(position) + " / " + formatTime(player->duration()));
}

void MainWindow::updateDuration(qint64 duration)
{
    positionSlider->setRange(0, static_cast<int>(duration));

    timeLabel->setText(
        formatTime(player->position()) + " / " + formatTime(duration));
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void MainWindow::refreshAudioDevices()
{
    audioDevices = QMediaDevices::audioOutputs();

    audioDeviceBox->blockSignals(true);
    audioDeviceBox->clear();

    int selectedIndex = -1;

    for (int i = 0; i < audioDevices.size(); ++i) {
        const QAudioDevice &device = audioDevices.at(i);

        audioDeviceBox->addItem(device.description());

        if (!currentDeviceId.isEmpty() && device.id() == currentDeviceId)
            selectedIndex = i;
    }

    if (selectedIndex < 0) {
        const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();

        for (int i = 0; i < audioDevices.size(); ++i) {
            if (audioDevices.at(i).id() == defaultDevice.id()) {
                selectedIndex = i;
                break;
            }
        }
    }

    if (selectedIndex >= 0)
        audioDeviceBox->setCurrentIndex(selectedIndex);

    audioDeviceBox->blockSignals(false);

    if (selectedIndex >= 0) {
        currentDeviceId = audioDevices.at(selectedIndex).id();
        audioOutput->setDevice(audioDevices.at(selectedIndex));
        surround->setDevice(audioDevices.at(selectedIndex));
    }
}

void MainWindow::selectAudioDevice(int index)
{
    if (index < 0 || index >= audioDevices.size())
        return;

    currentDeviceId = audioDevices.at(index).id();

    audioOutput->setDevice(audioDevices.at(index));
    surround->setDevice(audioDevices.at(index));

    statusLabel->setText(
        "Sortie audio : " + audioDevices.at(index).description());
}

void MainWindow::applyMatrixMode()
{
    const auto mode = static_cast<SurroundProcessor::MatrixMode>(matrixBox->currentData().toInt());
    surround->processor().setMatrixMode(mode);
    statusLabel->setText(matrixBox->currentText() + " sélectionné (DSP matriciel)");
}

void MainWindow::applyProfile()
{
    const auto profile = static_cast<SurroundProcessor::Profile>(profileBox->currentData().toInt());
    surround->processor().setProfile(profile);
    // Les curseurs reflètent les gains appliqués par le profil.
    surroundSlider->setValue(qRound(surround->processor().surroundLevel() * 100.0f));
    centerSlider->setValue(qRound(surround->processor().centerLevel() * 100.0f));
    lfeSlider->setValue(qRound(surround->processor().lfeLevel() * 100.0f));
    statusLabel->setText("Profil " + profileBox->currentText());
}

void MainWindow::applyLevels()
{
    auto &processor = surround->processor();

    processor.setSurroundLevel(surroundSlider->value() / 100.0f);
    processor.setCenterLevel(centerSlider->value() / 100.0f);
    processor.setLfeLevel(lfeSlider->value() / 100.0f);
}

void MainWindow::applyAudioEnhancements()
{
    const float gain = amplificationSlider->value() / 100.0f;
    surround->processor().setInputGain(gain);
    surround->processor().setVoiceNormalization(voiceNormalizeBox->isChecked());
    statusLabel->setText(QString("Traitement audio : amplification %1 %%, normalisation %2")
        .arg(amplificationSlider->value())
        .arg(voiceNormalizeBox->isChecked() ? "activée" : "désactivée"));
}

void MainWindow::applyVideoAspect()
{
    videoWidget->setAspectMode(aspectBox->currentIndex());
}

void MainWindow::applyVideoEffects()
{
    videoWidget->setDeinterlaceMode(deinterlaceBox->currentIndex());
    videoWidget->setSimulationMode(simulationBox->currentIndex());
    videoWidget->setReceptionMode(receptionBox->currentIndex());
    videoWidget->setSparklies(sparkliesBox->isChecked());
    statusLabel->setText("Effets vidéo appliqués");
}

void MainWindow::applyVolume()
{
    // Le curseur est en échelle logarithmique (perception de l'oreille) ;
    // Qt attend un volume linéaire.
    float linear = QtAudio::convertVolume(
        volumeSlider->value() / 100.0f,
        QtAudio::LogarithmicVolumeScale,
        QtAudio::LinearVolumeScale);

    if (muted)
        linear = 0.0f;

    // Tant que le DSP n'a pas reçu de trames valides, conserver la sortie
    // Qt directe : cela évite un lecteur muet si QAudioBufferOutput échoue.
    audioOutput->setVolume(surround->isReady() ? 0.0f : linear);
    surround->setVolume(linear);
}

void MainWindow::applyAudioRouting()
{
    const int selected = audioModeBox->currentData().toInt();
    if (selected == 5) {
        audioModeBox->blockSignals(true);
        audioModeBox->setCurrentIndex(0);
        audioModeBox->blockSignals(false);
        statusLabel->setText("Passthrough non disponible : Qt fournit ici du PCM décodé. Un chemin bitstream WASAPI dédié est nécessaire.");
        return;
    }

    if (selected == 2) {
        matrixBox->setCurrentIndex(int(SurroundProcessor::DolbySurround));
        profileBox->setCurrentIndex(int(SurroundProcessor::Standard));
        modeBox->setCurrentIndex(1);
    } else if (selected == 3) {
        matrixBox->setCurrentIndex(int(SurroundProcessor::ProLogicII));
        profileBox->setCurrentIndex(int(SurroundProcessor::Movie));
        modeBox->setCurrentIndex(1);
    } else if (selected == 4) {
        matrixBox->setCurrentIndex(int(SurroundProcessor::ProLogicII));
        profileBox->setCurrentIndex(int(SurroundProcessor::Music));
        modeBox->setCurrentIndex(1);
    } else {
        modeBox->setCurrentIndex(0);
    }

    applyAudioMode();
    applyMatrixMode();
    applyProfile();
    statusLabel->setText(audioApiBox->currentIndex() == 0
        ? "WASAPI via le backend Windows de Qt (mode partagé)"
        : "Backend audio système Qt");
}

void MainWindow::applyAudioMode()
{
    const int index = modeBox->currentIndex();

    // Même en stéréo, le flux passe par le DSP pour permettre
    // l'amplification et la normalisation sans activer l'upmix.
    surround->setRequestedMode(index == 0
        ? SurroundProcessor::Stereo
        : index == 1 ? SurroundProcessor::Surround51
                     : SurroundProcessor::Surround71);
    surround->setActive(true);
    player->setAudioBufferOutput(bufferOutput);

    if (index == 0)
        statusLabel->setText("Sortie stéréo avec traitements audio");
    else
        statusLabel->setText(index == 1 ? "Upmix 5.1" : "Upmix 7.1");

    applyVolume();
}

void MainWindow::toggleMute()
{
    muted = !muted;
    applyVolume();
    statusLabel->setText(muted ? "Son coupé" : "Son rétabli");
}

void MainWindow::refreshTracks()
{
    audioTrackBox->blockSignals(true);
    subtitleBox->blockSignals(true);

    audioTrackBox->clear();
    subtitleBox->clear();

    const auto audioTracks = player->audioTracks();
    for (int i = 0; i < audioTracks.size(); ++i)
        audioTrackBox->addItem(trackLabel(audioTracks.at(i), i, "Piste"));

    subtitleBox->addItem("Aucun sous-titre");

    const auto subtitleTracks = player->subtitleTracks();
    for (int i = 0; i < subtitleTracks.size(); ++i)
        subtitleBox->addItem(trackLabel(subtitleTracks.at(i), i, "Sous-titres"));

    if (!audioTracks.isEmpty())
        audioTrackBox->setCurrentIndex(
            std::max(0, player->activeAudioTrack()));

    subtitleBox->setCurrentIndex(player->activeSubtitleTrack() + 1);

    audioTrackBox->blockSignals(false);
    subtitleBox->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Plein écran, clavier, glisser-déposer
// ---------------------------------------------------------------------------

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
        hideControlsTimer->stop();
        controlsPanel->show();
    } else {
        showFullScreen();
        controlsPanel->hide();
    }
}

void MainWindow::showControlsTemporarily()
{
    controlsPanel->show();
    hideControlsTimer->start();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == videoWidget) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            toggleFullscreen();
            return true;
        }

        if (event->type() == QEvent::MouseMove && isFullScreen())
            showControlsTemporarily();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const qint64 step = shift ? 30000 : 5000;

    switch (event->key()) {
    case Qt::Key_Space:
        playPause();
        break;
    case Qt::Key_Left:
        seekTo(player->position() - step);
        break;
    case Qt::Key_Right:
        seekTo(player->position() + step);
        break;
    case Qt::Key_Up:
        volumeSlider->setValue(volumeSlider->value() + 5);
        break;
    case Qt::Key_Down:
        volumeSlider->setValue(volumeSlider->value() - 5);
        break;
    case Qt::Key_M:
        toggleMute();
        break;
    case Qt::Key_F:
    case Qt::Key_F11:
        toggleFullscreen();
        break;
    case Qt::Key_Escape:
        if (isFullScreen())
            toggleFullscreen();
        break;
    default:
        QMainWindow::keyPressEvent(event);
        return;
    }

    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();

    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            openPath(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Persistance
// ---------------------------------------------------------------------------

QString MainWindow::resumeKey(const QString &path) const
{
    const QByteArray hash = QCryptographicHash::hash(
        path.toUtf8(), QCryptographicHash::Sha1).toHex();

    return "resume/" + QString::fromLatin1(hash);
}

void MainWindow::saveResumePosition()
{
    if (currentPath.isEmpty())
        return;

    const qint64 position = player->position();
    const qint64 duration = player->duration();

    // On ne mémorise pas le tout début ni les 10 dernières secondes.
    if (duration > 0 && position > 5000 && position < duration - 10000)
        settings.setValue(resumeKey(currentPath), position);
    else
        settings.remove(resumeKey(currentPath));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveResumePosition();

    settings.setValue("geometry", saveGeometry());
    settings.setValue("volume", volumeSlider->value());
    settings.setValue("amplification", amplificationSlider->value());
    settings.setValue("voiceNormalization", voiceNormalizeBox->isChecked());
    settings.setValue("aspectMode", aspectBox->currentIndex());
    settings.setValue("deinterlaceMode", deinterlaceBox->currentIndex());
    settings.setValue("simulationMode", simulationBox->currentIndex());
    settings.setValue("receptionMode", receptionBox->currentIndex());
    settings.setValue("sparklies", sparkliesBox->isChecked());
    settings.setValue("mode", modeBox->currentIndex());
    settings.setValue("audioMode", audioModeBox->currentIndex());
    settings.setValue("audioApi", audioApiBox->currentIndex());
    settings.setValue("surroundLevel", surroundSlider->value());
    settings.setValue("centerLevel", centerSlider->value());
    settings.setValue("lfeLevel", lfeSlider->value());
    settings.setValue("audioDevice", currentDeviceId);

    QMainWindow::closeEvent(event);
}

QString MainWindow::formatTime(qint64 milliseconds) const
{
    const qint64 seconds = milliseconds / 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 remainingSeconds = seconds % 60;

    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(remainingSeconds, 2, 10, QChar('0'));
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(remainingSeconds, 2, 10, QChar('0'));
}
