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
    
    // Set up video widget
    m_videoWidget->setMinimumSize(640, 360);
    m_videoWidget->setStyleSheet("background-color: black; border: 2px solid #ff00ff;");
    
    // Status label
    m_statusLabel = new QLabel("Video player ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-size: 14px; color: #ff00ff;");
    
    // Time label
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet("font-family: monospace;");
    
    // Buttons
    m_playButton = new QPushButton(this);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setEnabled(false);
    
    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setEnabled(false);
    
    m_prevFrameButton = new QPushButton("⏮ Prev Frame", this);
    m_prevFrameButton->setEnabled(false);

    m_nextFrameButton = new QPushButton("Next Frame ⏭", this);
    m_nextFrameButton->setEnabled(false);

    m_loopButton = new QPushButton("Loop", this);
    m_loopButton->setCheckable(true);
    m_loopButton->setChecked(false);
    m_loopButton->setToolTip("Loop: replay the video continuously when it ends");

    m_popOutButton = new QPushButton("\u2922", this);  // ⤢
    m_popOutButton->setFixedSize(28, 28);
    m_popOutButton->setToolTip("Pop preview out to a floating window (dual-monitor)");

    // Position slider
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);
    
    // Button layout
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_prevFrameButton);
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addWidget(m_nextFrameButton);
    buttonLayout->addWidget(m_loopButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_popOutButton);
    
    // Slider layout
    auto *sliderLayout = new QHBoxLayout;
    sliderLayout->addWidget(m_timeLabel);
    sliderLayout->addWidget(m_positionSlider);
    
    // Add all widgets to main layout
    mainLayout->addWidget(m_videoWidget, 1);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(sliderLayout);
    mainLayout->addLayout(buttonLayout);
    
    // Connect UI signals
    connect(m_playButton, &QPushButton::clicked, this, &PreviewPlayer::playPause);
    connect(m_stopButton, &QPushButton::clicked, this, &PreviewPlayer::stop);
    connect(m_nextFrameButton, &QPushButton::clicked, this, &PreviewPlayer::nextFrame);
    connect(m_prevFrameButton, &QPushButton::clicked, this, &PreviewPlayer::previousFrame);
    connect(m_positionSlider, &QSlider::sliderMoved, this, &PreviewPlayer::onSliderMoved);

    connect(m_loopButton, &QPushButton::toggled, this, [this](bool on) {
        m_mediaPlayer->setLoops(on ? QMediaPlayer::Infinite : 1);
    });

    connect(m_popOutButton, &QPushButton::clicked, this, &PreviewPlayer::popOutRequested);
}

void PreviewPlayer::unloadVideo()
{
    m_mediaPlayer->stop();
    m_mediaPlayer->setSource(QUrl());  // release file handle
    m_playButton->setEnabled(false);
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
    m_stopButton->setEnabled(true);
    m_prevFrameButton->setEnabled(true);
    m_nextFrameButton->setEnabled(true);

    m_statusLabel->setText("Loaded: " + fileName);

    // Auto-play so the re-encoded result is immediately visible after Apply.
    m_mediaPlayer->play();
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
}

void PreviewPlayer::playPause()
{
    if (m_mediaPlayer->isPlaying()) {
        m_mediaPlayer->pause();
        m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_statusLabel->setText("Paused");
    } else {
        m_mediaPlayer->play();
        m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        m_statusLabel->setText("Playing...");
    }
}

void PreviewPlayer::stop()
{
    m_mediaPlayer->stop();
    m_positionSlider->setValue(0);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
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
