#include "QuickMoshWidget.h"

#include "core/presets/PresetManager.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>

// =============================================================================

QuickMoshWidget::QuickMoshWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 4);
    root->setSpacing(4);

    // ── Banner area: background image with progress bar ──────────────────────
    {
        auto* banner = new QWidget(this);
        banner->setFixedHeight(80);
        banner->setStyleSheet(
            "background-image: url(:/assets/png/Load_Progress_BG_LaMoshPit.png);"
            "background-repeat: no-repeat;"
            "background-position: center;"
            "border-bottom: 1px solid #330033;");

        auto* bannerLayout = new QVBoxLayout(banner);
        bannerLayout->setContentsMargins(16, 8, 16, 8);
        bannerLayout->setSpacing(4);

        // "Operation in progress..." status label
        m_opLabel = new QLabel("Operation in progress...", banner);
        m_opLabel->setAlignment(Qt::AlignCenter);
        m_opLabel->setVisible(false);
        m_opLabel->setStyleSheet(
            "color:#00ff88; font:bold 8pt 'Consolas'; "
            "background:#0a0a0a; border:none; padding:2px 8px; border-radius:3px;");
        bannerLayout->addWidget(m_opLabel);

        // Progress bar with percentage text
        m_progressBar = new QProgressBar(banner);
        m_progressBar->setFixedHeight(16);
        m_progressBar->setRange(0, 100);
        m_progressBar->setVisible(false);
        m_progressBar->setTextVisible(true);
        m_progressBar->setFormat("%p%");
        m_progressBar->setAlignment(Qt::AlignCenter);
        m_progressBar->setStyleSheet(
            "QProgressBar { background:#0a0a0aCC; border:1px solid #00ff88; border-radius:6px; "
            "color:#00ff88; font:bold 8pt 'Consolas'; }"
            "QProgressBar::chunk { background:qlineargradient("
            "x1:0,y1:0,x2:1,y2:0, stop:0 #003311, stop:0.5 #00ff88, stop:1 #003311); "
            "border-radius:5px; }");
        bannerLayout->addWidget(m_progressBar);
        bannerLayout->addStretch(1);

        root->addWidget(banner);
    }

    // ── "Quick Mosh" heading centered over the controls ──────────────────────
    auto* header = new QLabel("QUICK MOSH ZONE", this);
    header->setAlignment(Qt::AlignCenter);
    header->setStyleSheet(
        "font: bold 10pt 'Consolas'; color:#ff00ff; "
        "background:transparent; padding:2px 0;");
    root->addWidget(header);

    // ── Combo + Mosh Now row ─────────────────────────────────────────────────
    auto* controlsArea = new QWidget(this);
    controlsArea->setContentsMargins(0, 0, 0, 0);
    auto* controlsLayout = new QVBoxLayout(controlsArea);
    controlsLayout->setContentsMargins(4, 0, 4, 0);
    controlsLayout->setSpacing(4);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);

    auto* comboLabel = new QLabel("Preset:", controlsArea);
    comboLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
    topRow->addWidget(comboLabel);

    m_combo = new QComboBox(controlsArea);
    m_combo->setStyleSheet(
        "QComboBox { background:#1a1a1a; color:#ff88ff; border:1px solid #663366; "
        "font:bold 9pt 'Consolas'; padding:2px 6px; min-width:160px; }"
        "QComboBox::drop-down { border:none; width:20px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#ff88ff; "
        "selection-background-color:#441144; font:9pt 'Consolas'; }");
    topRow->addWidget(m_combo, 1);

    m_btnMosh = new QPushButton("Mosh Now!", controlsArea);
    m_btnMosh->setFixedHeight(32);
    m_btnMosh->setMinimumWidth(110);
    m_btnMosh->setEnabled(false);
    m_btnMosh->setStyleSheet(
        "QPushButton { background:#330033; color:#ff00ff; "
        "border:2px solid #ff00ff; border-radius:4px; font:bold 11pt 'Consolas'; }"
        "QPushButton:hover { background:#550055; color:#ffffff; border-color:#ffffff; }"
        "QPushButton:pressed { background:#ff00ff; color:#000000; }"
        "QPushButton:disabled { background:#1a1a1a; color:#443344; border-color:#332233; }");
    topRow->addWidget(m_btnMosh);
    controlsLayout->addLayout(topRow);

    // Preset management buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    m_btnUserSave   = new QPushButton("Save",   controlsArea);
    m_btnUserDel    = new QPushButton("Del",    controlsArea);
    m_btnUserImport = new QPushButton("Import", controlsArea);

    const QString btnSS =
        "QPushButton { background:#1a1a1a; color:#aaa; border:1px solid #444; "
        "font:7pt 'Consolas'; padding:2px 5px; border-radius:2px; }"
        "QPushButton:hover { background:#222; color:#fff; border-color:#666; }"
        "QPushButton:disabled { color:#333; border-color:#222; }";
    m_btnUserSave  ->setStyleSheet(btnSS);
    m_btnUserDel   ->setStyleSheet(btnSS);
    m_btnUserImport->setStyleSheet(btnSS);

    btnRow->addWidget(m_btnUserSave);
    btnRow->addWidget(m_btnUserDel);
    btnRow->addWidget(m_btnUserImport);
    btnRow->addStretch();
    controlsLayout->addLayout(btnRow);

    // Description label
    m_desc = new QLabel("Save MB Editor + Global Encode Params as a Quick Mosh preset, "
                        "or import one from a friend.", controlsArea);
    m_desc->setWordWrap(true);
    m_desc->setStyleSheet("color:#888; font:8pt 'Consolas'; padding:2px 0;");
    m_desc->setMinimumHeight(32);
    controlsLayout->addWidget(m_desc);

    root->addWidget(controlsArea);
    root->addStretch(1);

    refreshUserPresets();

    // Wire signals
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (m_btnUserDel)
            m_btnUserDel->setEnabled(idx >= 0 &&
                !m_combo->currentText().startsWith('\xe2'));
    });

    connect(m_btnMosh, &QPushButton::clicked, this, [this]() {
        const QString name = m_combo->currentText();
        if (!name.isEmpty() && !name.startsWith('\xe2'))
            emit userMoshRequested(name);
    });
    connect(m_btnUserSave,   &QPushButton::clicked,
            this, &QuickMoshWidget::saveUserPresetRequested);
    connect(m_btnUserDel,    &QPushButton::clicked,
            this, &QuickMoshWidget::onUserPresetDelete);
    connect(m_btnUserImport, &QPushButton::clicked,
            this, &QuickMoshWidget::onUserPresetImport);
}

void QuickMoshWidget::setMoshEnabled(bool enabled)
{
    m_btnMosh->setEnabled(enabled);
}

void QuickMoshWidget::setProgressVisible(bool visible)
{
    m_progressBar->setVisible(visible);
    m_opLabel->setVisible(visible);
}

void QuickMoshWidget::refreshUserPresets()
{
    if (!m_combo) return;
    QSignalBlocker sb(m_combo);
    m_combo->clear();

    const QStringList names = PresetManager::list(PresetManager::Type::QuickMosh);
    if (names.isEmpty()) {
        m_combo->addItem("\xe2\x80\x94 no presets saved \xe2\x80\x94");
    } else {
        for (const QString& n : names)
            m_combo->addItem(n);
    }

    const bool hasPresets = !names.isEmpty();
    if (m_btnUserDel) m_btnUserDel->setEnabled(hasPresets);
}

void QuickMoshWidget::onUserPresetDelete()
{
    const QString name = m_combo->currentText();
    if (name.isEmpty() || name.startsWith('\xe2')) return;

    const auto btn = QMessageBox::question(this, "Delete Preset",
        QString("Delete Quick Mosh preset \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    PresetManager::deletePreset(PresetManager::Type::QuickMosh, name);
    refreshUserPresets();
}

void QuickMoshWidget::onUserPresetImport()
{
    const QString src = QFileDialog::getOpenFileName(this,
        "Import Quick Mosh Preset", QString(),
        "JSON Preset Files (*.json);;All Files (*)");
    if (src.isEmpty()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this,
        "Import Preset",
        "Name for imported preset:",
        QLineEdit::Normal,
        QFileInfo(src).completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (!PresetManager::importFile(PresetManager::Type::QuickMosh, src, name)) {
        QMessageBox::warning(this, "Import Failed",
            "The selected file does not appear to be a Quick Mosh preset.");
        return;
    }
    refreshUserPresets();
    const int idx = m_combo->findText(PresetManager::sanitize(name));
    if (idx >= 0) m_combo->setCurrentIndex(idx);
}
