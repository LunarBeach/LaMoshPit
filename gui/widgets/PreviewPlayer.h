#pragma once
#include <QWidget>
#include <QString>

// Qt6 includes
#include <QMediaPlayer>
#include <QVideoWidget>

class QLabel;
class QPushButton;
class QSlider;

class PreviewPlayer : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPlayer(QWidget *parent = nullptr);
    void loadVideo(const QString &filePath);
    void unloadVideo();
    void updateFrame(int frameIndex);
    void setStatus(const QString &status);

private slots:
    void playPause();
    void stop();
    void nextFrame();
    void previousFrame();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSliderMoved(int value);

private:
    void setupUI();
    void updateTimeDisplay();
    
    QMediaPlayer *m_mediaPlayer;
    QVideoWidget *m_videoWidget;
    
    QLabel *m_statusLabel;
    QLabel *m_timeLabel;
    QPushButton *m_playButton;
    QPushButton *m_stopButton;
    QPushButton *m_nextFrameButton;
    QPushButton *m_prevFrameButton;
    QSlider *m_positionSlider;
    
    QString m_currentVideoPath;
    qint64 m_duration;
};
