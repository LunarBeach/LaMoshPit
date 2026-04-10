#pragma once
#include <QWidget>

class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget *parent = nullptr);
    void setSelectedFrame(const class HackedFrame& frame);  // Switches I/P/B UI
};