#pragma once

#include <QAbstractButton>
#include <QColor>

// =============================================================================
// ToggleSwitch — drop-in replacement for QCheckBox
//
// Renders a guitar-amp-inspired metal toggle switch with a small LED indicator
// above the track that glows when the switch is ON.
//
// API is fully compatible with QCheckBox:
//   isChecked() / setChecked(bool)  — inherited from QAbstractButton
//   toggled(bool)                   — inherited signal
//   setText() / text()              — inherited
//
// Optional: setOnColor() to change the LED + handle accent colour.
// =============================================================================
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(const QString& text = {}, QWidget* parent = nullptr);

    // Accent colour used for handle and LED when ON.  Default: neon green #00ff88.
    void setOnColor(const QColor& c);

    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*)  override;
    void leaveEvent(QEvent*)       override;

private:
    QColor m_onColor  { 0x00, 0xff, 0x88 };
    bool   m_hovered  { false };

    // Geometry constants
    static constexpr int kLedD    =  6;   // LED dot diameter (px)
    static constexpr int kLedGap  =  2;   // gap between LED bottom and track top
    static constexpr int kTrkW    = 42;   // track width
    static constexpr int kTrkH    = 20;   // track height
    static constexpr int kHandleD = 16;   // handle (knob) diameter
    static constexpr int kLabelGap=  8;   // gap between track right edge and label
    static constexpr int kTotalH  = kLedD + kLedGap + kTrkH; // 28 px

    QRectF trackRect()  const;
    QRectF ledRect()    const;
    QRectF handleRect() const;
};
