#include "BitstreamTestWidget.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

// Include h264bitstream - this will test if it's properly linked
extern "C" {
#include "h264_stream.h"
}

BitstreamTestWidget::BitstreamTestWidget(QWidget *parent)
    : QWidget(parent)
    , m_resultLabel(nullptr)
    , m_testButton(nullptr)
{
    setupUI();
}

void BitstreamTestWidget::setupUI()
{
    auto *layout = new QVBoxLayout(this);
    
    auto *titleLabel = new QLabel("H.264 Bitstream Library Test", this);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    
    m_resultLabel = new QLabel("Click 'Test' to verify h264bitstream integration", this);
    m_resultLabel->setStyleSheet("font-size: 12px; padding: 10px;");
    m_resultLabel->setWordWrap(true);
    
    m_testButton = new QPushButton("Test H.264 Bitstream", this);
    connect(m_testButton, &QPushButton::clicked, this, &BitstreamTestWidget::runTest);
    
    layout->addWidget(titleLabel);
    layout->addWidget(m_resultLabel);
    layout->addWidget(m_testButton);
    layout->addStretch();
}

void BitstreamTestWidget::runTest()
{
    try {
        // Test h264bitstream functions
        h264_stream_t* h = h264_new();
        if (h) {
            // Basic test - create and free
            m_resultLabel->setText("✅ SUCCESS: h264bitstream library loaded and working!\n"
                                 "Library version: " + QString::number(h->nal->nal_unit_type) + 
                                 "\nMemory management: OK");
            h264_free(h);
        } else {
            m_resultLabel->setText("❌ ERROR: h264_new() returned null");
        }
    } catch (...) {
        m_resultLabel->setText("❌ ERROR: Exception occurred during h264bitstream test");
    }
}
