#pragma once
#include <QWidget>

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);
    void setVideoSequence(class VideoSequence* sequence);

signals:
    void frameSelected(int frameIndex);
};