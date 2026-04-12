#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

// =============================================================================
// QuickMoshWidget
//
// A compact "one-click datamosh" panel.  The user picks a named effect from
// the combo and clicks "Mosh Now" — the panel emits moshRequested(index) so
// MainWindow can build the correct MBEditMap and fire the encode pipeline.
//
// Presets are whole-video strategies: cascade effects seed one frame and let
// the cascade fill the rest; per-frame effects stamp params on every frame.
// MainWindow owns the frame count and first-P-frame lookup so it can set
// cascadeLen correctly without the widget needing to know video internals.
// =============================================================================
class QuickMoshWidget : public QWidget {
    Q_OBJECT
public:
    explicit QuickMoshWidget(QWidget* parent = nullptr);

    // Enable/disable the Mosh Now button (MainWindow disables during encode).
    void setMoshEnabled(bool enabled);

signals:
    void moshRequested(int presetIndex);

private:
    QComboBox*   m_combo;
    QLabel*      m_desc;
    QPushButton* m_btnMosh;
};
