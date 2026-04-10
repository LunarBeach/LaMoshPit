#pragma once
#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;

class BitstreamTestWidget : public QWidget {
    Q_OBJECT
public:
    explicit BitstreamTestWidget(QWidget *parent = nullptr);
    
private slots:
    void runTest();
    
private:
    void setupUI();
    QLabel *m_resultLabel;
    QPushButton *m_testButton;
};
