#include "ToggleSwitch.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QEnterEvent>

// =============================================================================
// Construction
// =============================================================================

ToggleSwitch::ToggleSwitch(const QString& text, QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);        // clicking toggles checked / unchecked
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setText(text);
}

void ToggleSwitch::setOnColor(const QColor& c)
{
    m_onColor = c;
    update();
}

// =============================================================================
// Size
// =============================================================================

QSize ToggleSwitch::sizeHint() const
{
    int textW = 0;
    if (!text().isEmpty()) {
        QFontMetrics fm(font());
        textW = fm.horizontalAdvance(text()) + kLabelGap;
    }
    return QSize(kTrkW + textW, kTotalH);
}

QSize ToggleSwitch::minimumSizeHint() const
{
    return sizeHint();
}

// =============================================================================
// Geometry helpers
// =============================================================================

QRectF ToggleSwitch::trackRect() const
{
    // Track sits at the bottom of the widget, left-aligned.
    float y = (float)(kLedD + kLedGap);
    return QRectF(0, y, kTrkW, kTrkH);
}

QRectF ToggleSwitch::ledRect() const
{
    // LED centred horizontally above the track.
    float cx = kTrkW * 0.5f;
    return QRectF(cx - kLedD * 0.5f, 0, kLedD, kLedD);
}

QRectF ToggleSwitch::handleRect() const
{
    QRectF tr = trackRect();
    float margin = 2.0f;
    float x = isChecked()
        ? tr.right()  - kHandleD - margin
        : tr.left()   + margin;
    float y = tr.top() + (kTrkH - kHandleD) * 0.5f;
    return QRectF(x, y, kHandleD, kHandleD);
}

// =============================================================================
// Paint
// =============================================================================

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool on  = isChecked();
    const bool hov = m_hovered;

    QRectF tr = trackRect();
    QRectF lr = ledRect();
    QRectF hr = handleRect();

    // ── Track ─────────────────────────────────────────────────────────────────
    QPainterPath trackPath;
    trackPath.addRoundedRect(tr, kTrkH * 0.5f, kTrkH * 0.5f);

    QColor trackFill = on
        ? QColor(m_onColor.red()   / 7,
                 m_onColor.green() / 5,
                 m_onColor.blue()  / 7)
        : (hov ? QColor(0x22, 0x22, 0x22) : QColor(0x16, 0x16, 0x16));

    QColor trackBorder = on
        ? m_onColor.darker(250)
        : QColor(0x32, 0x32, 0x32);

    p.fillPath(trackPath, trackFill);

    // Metallic sheen gradient inside track
    QLinearGradient sheen(tr.topLeft(), tr.bottomLeft());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 22));
    sheen.setColorAt(0.4, QColor(255, 255, 255,  5));
    sheen.setColorAt(1.0, QColor(  0,   0,   0, 35));
    p.fillPath(trackPath, sheen);

    p.setPen(QPen(trackBorder, 1.0));
    p.drawPath(trackPath);

    // Recessed inset shadow at top of track
    QPainterPath innerShadow;
    innerShadow.addRoundedRect(tr.adjusted(1, 1, -1, -kTrkH * 0.6f),
                               kTrkH * 0.3f, kTrkH * 0.3f);
    p.fillPath(innerShadow, QColor(0, 0, 0, 30));

    // ── LED indicator ─────────────────────────────────────────────────────────
    QRadialGradient ledGrad(lr.center(), lr.width() * 0.55);

    if (on) {
        // Glow halo
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(m_onColor.red(), m_onColor.green(), m_onColor.blue(), 55));
        p.drawEllipse(lr.adjusted(-4, -4, 4, 4));

        ledGrad.setColorAt(0.0, m_onColor.lighter(140));
        ledGrad.setColorAt(0.55, m_onColor);
        ledGrad.setColorAt(1.0, m_onColor.darker(200));
    } else {
        ledGrad.setColorAt(0.0, QColor(0x38, 0x38, 0x38));
        ledGrad.setColorAt(0.6, QColor(0x22, 0x22, 0x22));
        ledGrad.setColorAt(1.0, QColor(0x10, 0x10, 0x10));
    }

    p.setPen(QPen(QColor(0x15, 0x15, 0x15), 0.8));
    p.setBrush(ledGrad);
    p.drawEllipse(lr);

    // Tiny specular reflection on LED
    if (on) {
        QRectF spec = lr.adjusted(lr.width() * 0.15, lr.height() * 0.1,
                                  -lr.width() * 0.4, -lr.height() * 0.5);
        QRadialGradient sg(spec.center(), spec.width() * 0.5);
        sg.setColorAt(0.0, QColor(255, 255, 255, 130));
        sg.setColorAt(1.0, QColor(255, 255, 255,   0));
        p.setPen(Qt::NoPen);
        p.setBrush(sg);
        p.drawEllipse(spec);
    }

    // ── Handle (sliding knob) ─────────────────────────────────────────────────
    // Offset the gradient centre up-left to fake a sphere highlight
    QPointF hc = hr.center() + QPointF(-hr.width() * 0.14, -hr.height() * 0.14);
    QRadialGradient hGrad(hc, hr.width() * 0.65);

    if (on) {
        hGrad.setColorAt(0.0, m_onColor.lighter(120));
        hGrad.setColorAt(0.6, m_onColor);
        hGrad.setColorAt(1.0, m_onColor.darker(170));
    } else {
        QColor base = hov ? QColor(0x58, 0x58, 0x58) : QColor(0x4a, 0x4a, 0x4a);
        hGrad.setColorAt(0.0, base.lighter(120));
        hGrad.setColorAt(0.6, base);
        hGrad.setColorAt(1.0, base.darker(160));
    }

    // Drop shadow under handle
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(hr.adjusted(1.5, 1.5, 1.5, 1.5));

    // Handle body
    p.setPen(QPen(QColor(0x12, 0x12, 0x12), 0.8));
    p.setBrush(hGrad);
    p.drawEllipse(hr);

    // Specular highlight on handle
    QRectF specH = hr.adjusted(hr.width() * 0.1,  hr.height() * 0.08,
                               -hr.width() * 0.45, -hr.height() * 0.5);
    QRadialGradient spH(specH.center(), specH.width() * 0.6);
    spH.setColorAt(0.0, QColor(255, 255, 255, on ? 90 : 70));
    spH.setColorAt(1.0, QColor(255, 255, 255,  0));
    p.setPen(Qt::NoPen);
    p.setBrush(spH);
    p.drawEllipse(specH);

    // ── Label text ────────────────────────────────────────────────────────────
    if (!text().isEmpty()) {
        QColor textCol = on ? QColor(0xcc, 0xff, 0xea) : QColor(0x77, 0x77, 0x77);
        if (hov && !on) textCol = QColor(0x99, 0x99, 0x99);
        p.setPen(textCol);
        p.setFont(font());
        int textX = kTrkW + kLabelGap;
        QRect textRect(textX, 0, width() - textX, height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text());
    }
}

// =============================================================================
// Hover tracking (for handle shading)
// =============================================================================

void ToggleSwitch::enterEvent(QEnterEvent*) { m_hovered = true;  update(); }
void ToggleSwitch::leaveEvent(QEvent*)      { m_hovered = false; update(); }
