#include "QuickMoshWidget.h"
#include "gui/AppFonts.h"

#include "core/presets/PresetManager.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
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
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Combo + Mosh Now row ─────────────────────────────────────────────────
    auto* controlsArea = new QWidget(this);
    controlsArea->setContentsMargins(0, 0, 0, 0);
    auto* controlsLayout = new QVBoxLayout(controlsArea);
    controlsLayout->setContentsMargins(4, 0, 4, 0);
    controlsLayout->setSpacing(4);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);

    auto* comboLabel = new QLabel("Preset:", controlsArea);
    comboLabel->setStyleSheet(QString(
        "QLabel { color:#bbbbbb; font-family:'%1'; font-size:10pt; background:transparent; }"
    ).arg(AppFonts::bodyFamily()));
    topRow->addWidget(comboLabel);

    m_combo = new QComboBox(controlsArea);
    m_combo->setStyleSheet(
        "QComboBox { background:#1a1a1a; color:#00ff88; border:1px solid #1a6633; "
        "font-weight:bold; padding:2px 6px; min-width:160px; }"
        "QComboBox::drop-down { border:none; width:20px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#00ff88; "
        "selection-background-color:#114433; }");
    topRow->addWidget(m_combo, 1);

    m_btnMosh = new QPushButton("Mosh Now!", controlsArea);
    m_btnMosh->setFixedHeight(32);
    m_btnMosh->setMinimumWidth(110);
    m_btnMosh->setEnabled(false);
    m_btnMosh->setStyleSheet(
        "QPushButton { background:#0a2a1a; color:#00ff88; "
        "border:2px solid #00ff88; border-radius:4px; font-weight:bold; font-size:11pt; }"
        "QPushButton:hover { background:#1a4a2a; color:#ffffff; border-color:#ffffff; }"
        "QPushButton:pressed { background:#00ff88; color:#000000; }"
        "QPushButton:disabled { background:#1a1a1a; color:#334433; border-color:#223322; }");
    topRow->addWidget(m_btnMosh);
    controlsLayout->addLayout(topRow);

    // Preset management buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    m_btnUserSave   = new QPushButton("Save",   controlsArea);
    m_btnUserDel    = new QPushButton("Del",    controlsArea);
    m_btnUserImport = new QPushButton("Import", controlsArea);

    const QString btnSS = QString(
        "QPushButton { background:#1a1a1a; color:#aaa; border:1px solid #444; "
        "font-family:'%1','%2'; font-size:9pt; padding:5px 12px; border-radius:3px; }"
        "QPushButton:hover { background:#222; color:#fff; border-color:#666; }"
        "QPushButton:disabled { color:#333; border-color:#222; }"
    ).arg(AppFonts::displayFamily(), AppFonts::bodyFamily());
    m_btnUserSave  ->setStyleSheet(btnSS);
    m_btnUserDel   ->setStyleSheet(btnSS);
    m_btnUserImport->setStyleSheet(btnSS);
    m_btnUserSave  ->setMinimumHeight(28);
    m_btnUserDel   ->setMinimumHeight(28);
    m_btnUserImport->setMinimumHeight(28);

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
