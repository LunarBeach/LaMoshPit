#include "ProgressPanel.h"

#include <QVBoxLayout>
#include <QProgressBar>
#include <QLabel>

ProgressPanel::ProgressPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("ProgressPanel");
    setStyleSheet(
        "QWidget#ProgressPanel { background:#0a0a0a; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    m_statusLbl = new QLabel("Operation in progress...", this);
    m_statusLbl->setAlignment(Qt::AlignCenter);
    m_statusLbl->setVisible(false);
    m_statusLbl->setStyleSheet(
        "color:#00ff88; background:#0a0a0a; border:none; "
        "padding:3px 8px; border-radius:3px; font-weight:bold;");
    root->addWidget(m_statusLbl);

    m_bar = new QProgressBar(this);
    m_bar->setMinimumHeight(18);
    m_bar->setRange(0, 100);
    m_bar->setVisible(false);
    m_bar->setTextVisible(true);
    m_bar->setFormat("%p%");
    m_bar->setAlignment(Qt::AlignCenter);
    m_bar->setStyleSheet(
        "QProgressBar { background:#0a0a0a; border:1px solid #00ff88; "
        "border-radius:6px; color:#00ff88; font-weight:bold; }"
        "QProgressBar::chunk { background:qlineargradient("
        "x1:0,y1:0,x2:1,y2:0, stop:0 #003311, stop:0.5 #00ff88, stop:1 #003311); "
        "border-radius:5px; }");
    root->addWidget(m_bar);
    root->addStretch(1);
}

void ProgressPanel::setRange(int minimum, int maximum)
{
    m_bar->setRange(minimum, maximum);
}

void ProgressPanel::setValue(int value)
{
    m_bar->setValue(value);
}

void ProgressPanel::setProgressVisible(bool visible)
{
    m_bar->setVisible(visible);
    m_statusLbl->setVisible(visible);
}

void ProgressPanel::setStatusText(const QString& text)
{
    m_statusLbl->setText(text);
}
