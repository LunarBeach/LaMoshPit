#include "PreviewPlayer.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QFileInfo>
#include <QTime>
#include <QUrl>
#include <QGraphicsOpacityEffect>

// Qt6 Multimedia includes
#include <QMediaPlayer>
#include <QVideoWidget>

PreviewPlayer::PreviewPlayer(QWidget *parent)
    : QWidget(parent)
    , m_mediaPlayer(new QMediaPlayer(this))
    , m_videoWidget(new QVideoWidget(this))
    , m_statusLabel(nullptr)
    , m_timeLabel(nullptr)
    , m_playButton(nullptr)
    , m_stopButton(nullptr)
    , m_nextFrameButton(nullptr)
    , m_prevFrameButton(nullptr)
    , m_loopButton(nullptr)
    , m_popOutButton(nullptr)
    , m_positionSlider(nullptr)
    , m_duration(0)
{
    setupUI();

    // Wire the video output once here — calling setVideoOutput after setSource
    // on the WMF backend can silently drop the pipeline, so we set it up once
    // at construction and never touch it again.
    m_mediaPlayer->setVideoOutput(m_videoWidget);

    // Connect media player signals
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &PreviewPlayer::onPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::durationChanged, this, &PreviewPlayer::onDurationChanged);
}

void PreviewPlayer::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Set up video widget
    // Kept low so the containing dock can be resized freely by the user.
    // Aspect ratio of the video is preserved by QVideoWidget regardless.
    m_videoWidget->setMinimumSize(160, 90);
    m_videoWidget->setStyleSheet("background-color: black; border: 2px solid #00ff88;");

    // Status label
    m_statusLabel = new QLabel("Video player ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-size: 14px; color: #00ff88;");

    // Time label
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet("font-family: 'Consolas'; color:#888; font-size:9pt;");

    // ── Icon button helper ───────────────────────────────────────────────────
    const int btnSize = 32;
    const int iconSize = 28;
    const QString flatBtnSS =
        "QPushButton { background:transparent; border:none; }"
        "QPushButton:hover { background:#222; border-radius:4px; }"
        "QPushButton:pressed { background:#333; border-radius:4px; }"
        "QPushButton:disabled { opacity:0.3; }";

    auto makeIconBtn = [&](const QString& iconPath, const QString& tooltip) -> QPushButton* {
        auto* btn = new QPushButton(this);
        btn->setFixedSize(btnSize, btnSize);
        btn->setIconSize(QSize(iconSize, iconSize));
        btn->setIcon(QIcon(iconPath));
        btn->setToolTip(tooltip);
        btn->setStyleSheet(flatBtnSS);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    // Buttons — order: Previous, Pause, Stop, Play, Next, Loop
    m_prevFrameButton = makeIconBtn(":/assets/png/Buttons/Previous_Frame.png", "Previous");
    m_pauseButton     = makeIconBtn(":/assets/png/Buttons/Pause_Button.png",   "Pause");
    m_stopButton      = makeIconBtn(":/assets/png/Buttons/Stop_Button.png",    "Stop");
    m_playButton      = makeIconBtn(":/assets/png/Buttons/Play_Button.png",    "Play");
    m_nextFrameButton = makeIconBtn(":/assets/png/Buttons/Next_Frame.png",     "Next");
    m_loopButton      = makeIconBtn(":/assets/png/Buttons/Loop_Button.png",    "Loop");

    m_prevFrameButton->setEnabled(false);
    m_pauseButton->setEnabled(false);
    m_stopButton->setEnabled(false);
    m_playButton->setEnabled(false);
    m_nextFrameButton->setEnabled(false);

    m_loopButton->setCheckable(true);
    m_loopButton->setChecked(false);

    // Loop button starts at 30% opacity (inactive)
    m_loopOpacity = new QGraphicsOpacityEffect(m_loopButton);
    m_loopOpacity->setOpacity(0.30);
    m_loopButton->setGraphicsEffect(m_loopOpacity);

    // Pop-out button (separate, right-aligned)
    m_popOutButton = new QPushButton("\u2922", this);  // ⤢
    m_popOutButton->setFixedSize(28, 28);
    m_popOutButton->setToolTip("Pop-out");
    m_popOutButton->setStyleSheet(
        "QPushButton { background:transparent; color:#888; border:1px solid #333; "
        "border-radius:4px; font:bold 11pt 'Consolas'; }"
        "QPushButton:hover { background:#222; color:#fff; border-color:#555; }");

    // Position slider
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background:#1e1e1e; height:5px; border-radius:2px; }"
        "QSlider::handle:horizontal { background:#00ff88; width:10px; height:10px; "
        "  margin:-3px 0; border-radius:5px; }"
        "QSlider::sub-page:horizontal { background:#004433; border-radius:2px; }");

    // ── Button row — centered group ──────────────────────────────────────────
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(6);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_prevFrameButton);
    buttonLayout->addWidget(m_pauseButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addWidget(m_nextFrameButton);
    buttonLayout->addWidget(m_loopButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_popOutButton);

    // Slider layout
    auto *sliderLayout = new QHBoxLayout;
    sliderLayout->addWidget(m_timeLabel);
    sliderLayout->addWidget(m_positionSlider, 1);

    // Add all widgets to main layout
    mainLayout->addWidget(m_videoWidget, 1);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(sliderLayout);
    mainLayout->addLayout(buttonLayout);

    // Connect UI signals
    connect(m_playButton, &QPushButton::clicked, this, &PreviewPlayer::playPause);
    connect(m_pauseButton, &QPushButton::clicked, this, &PreviewPlayer::playPause);
    connect(m_stopButton, &QPushButton::clicked, this, &PreviewPlayer::stop);
    connect(m_nextFrameButton, &QPushButton::clicked, this, &PreviewPlayer::nextFrame);
    connect(m_prevFrameButton, &QPushButton::clicked, this, &PreviewPlayer::previousFrame);
    connect(m_positionSlider, &QSlider::sliderMoved, this, &PreviewPlayer::onSliderMoved);

    connect(m_loopButton, &QPushButton::toggled, this, [this](bool on) {
        m_mediaPlayer->setLoops(on ? QMediaPlayer::Infinite : 1);
        m_loopOpacity->setOpacity(on ? 1.0 : 0.30);
    });

    connect(m_popOutButton, &QPushButton::clicked, this, &PreviewPlayer::popOutRequested);
}

void PreviewPlayer::unloadVideo()
{
    m_mediaPlayer->stop();
    m_mediaPlayer->setSource(QUrl());  // release file handle
    m_playButton->setEnabled(false);
    m_pauseButton->setEnabled(false);
    m_stopButton->setEnabled(false);
    m_prevFrameButton->setEnabled(false);
    m_nextFrameButton->setEnabled(false);
    m_statusLabel->setText("Video player ready");
}

void PreviewPlayer::loadVideo(const QString &filePath)
{
    m_currentVideoPath = filePath;
    QString fileName = QFileInfo(filePath).fileName();

    // Cache-bust the WMF backend: clearing the source before setting a new one
    // forces WMF to fully release the previous file handle and pipeline state.
    m_mediaPlayer->setSource(QUrl());
    m_mediaPlayer->setSource(QUrl::fromLocalFile(filePath));

    // Enable controls
    m_playButton->setEnabled(true);
    m_pauseButton->setEnabled(true);
    m_stopButton->setEnabled(true);
    m_prevFrameButton->setEnabled(true);
    m_nextFrameButton->setEnabled(true);

    m_statusLabel->setText("Loaded: " + fileName);

    // Auto-play so the re-encoded result is immediately visible after Apply.
    m_mediaPlayer->play();
}

void PreviewPlayer::playPause()
{
    if (m_mediaPlayer->isPlaying()) {
        m_mediaPlayer->pause();
        m_statusLabel->setText("Paused");
    } else {
        m_mediaPlayer->play();
        m_statusLabel->setText("Playing...");
    }
}

void PreviewPlayer::stop()
{
    m_mediaPlayer->stop();
    m_positionSlider->setValue(0);
    m_statusLabel->setText("Stopped");
}

void PreviewPlayer::nextFrame()
{
    qint64 pos = m_mediaPlayer->position();
    m_mediaPlayer->setPosition(pos + 40); // ~1 frame at 25fps
    m_statusLabel->setText("Frame step forward");
}

void PreviewPlayer::previousFrame()
{
    qint64 pos = m_mediaPlayer->position();
    m_mediaPlayer->setPosition(qMax(qint64(0), pos - 40)); // ~1 frame at 25fps
    m_statusLabel->setText("Frame step backward");
}

void PreviewPlayer::onPositionChanged(qint64 position)
{
    m_positionSlider->setValue(position);
    updateTimeDisplay();
}

void PreviewPlayer::onDurationChanged(qint64 duration)
{
    m_duration = duration;
    m_positionSlider->setRange(0, duration);
    updateTimeDisplay();
}

void PreviewPlayer::onSliderMoved(int value)
{
    m_mediaPlayer->setPosition(value);
}

void PreviewPlayer::updateTimeDisplay()
{
    if (m_duration <= 0) return;
    
    QTime currentTime((m_mediaPlayer->position()/3600000)%60, (m_mediaPlayer->position()/60000)%60, (m_mediaPlayer->position()/1000)%60);
    QTime totalTime((m_duration/3600000)%60, (m_duration/60000)%60, (m_duration/1000)%60);
    m_timeLabel->setText(currentTime.toString("mm:ss") + " / " + totalTime.toString("mm:ss"));
}

void PreviewPlayer::updateFrame(int /*frameIndex*/)
{
    // Refresh with current hacked state
}

void PreviewPlayer::setStatus(const QString &status)
{
    if (m_statusLabel) {
        m_statusLabel->setText(status);
    }
}
