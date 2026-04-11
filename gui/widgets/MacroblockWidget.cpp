#include "MacroblockWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QDial>
#include <QSlider>
#include <QSpinBox>
#include <QGroupBox>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// =============================================================================
// Async frame decode — runs on QtConcurrent thread pool.
// Sequential decode from frame 0 (single-GOP videos have no usable seek points
// except the IDR at 0).  Returns a QImage (RGB24, max 640 px wide).
// =============================================================================
static QImage decodeFrameAt(const QString& videoPath, int targetFrame)
{
    AVFormatContext* fmtCtx  = nullptr;
    AVCodecContext*  decCtx  = nullptr;
    SwsContext*      swsCtx  = nullptr;
    AVFrame*  yuvFrame       = nullptr;
    AVFrame*  rgbFrame       = nullptr;
    uint8_t*  rgbBuf         = nullptr;
    AVPacket* pkt            = nullptr;
    QImage    result;
    int vsIdx    = -1;
    int frameIdx = 0;
    int outW     = 0;
    int outH     = 0;

    if (avformat_open_input(&fmtCtx, videoPath.toUtf8().constData(),
                            nullptr, nullptr) < 0)
        goto done;
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
        goto done;

    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vsIdx = (int)i; break;
        }
    }
    if (vsIdx < 0) goto done;

    {
        const AVCodec* codec =
            avcodec_find_decoder(fmtCtx->streams[vsIdx]->codecpar->codec_id);
        if (!codec) goto done;
        decCtx = avcodec_alloc_context3(codec);
        if (!decCtx) goto done;
        if (avcodec_parameters_to_context(decCtx,
                fmtCtx->streams[vsIdx]->codecpar) < 0) goto done;
        if (avcodec_open2(decCtx, codec, nullptr) < 0) goto done;

        outW = qMin(decCtx->width, 640);
        outH = (outW * decCtx->height + decCtx->width / 2) / decCtx->width;
        if (outH < 1) outH = 1;

        yuvFrame = av_frame_alloc();
        rgbFrame = av_frame_alloc();
        {
            int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, outW, outH, 1);
            rgbBuf = (uint8_t*)av_malloc((size_t)bufSize);
            av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize,
                                 rgbBuf, AV_PIX_FMT_RGB24, outW, outH, 1);
        }
        pkt = av_packet_alloc();

        while (av_read_frame(fmtCtx, pkt) >= 0) {
            if (pkt->stream_index == vsIdx) {
                if (avcodec_send_packet(decCtx, pkt) == 0) {
                    while (avcodec_receive_frame(decCtx, yuvFrame) == 0) {
                        if (frameIdx == targetFrame) {
                            swsCtx = sws_getContext(
                                yuvFrame->width, yuvFrame->height,
                                (AVPixelFormat)yuvFrame->format,
                                outW, outH, AV_PIX_FMT_RGB24,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
                            if (swsCtx) {
                                sws_scale(swsCtx,
                                    yuvFrame->data, yuvFrame->linesize, 0,
                                    yuvFrame->height,
                                    rgbFrame->data, rgbFrame->linesize);
                                result = QImage(rgbBuf, outW, outH,
                                               rgbFrame->linesize[0],
                                               QImage::Format_RGB888).copy();
                            }
                            av_frame_unref(yuvFrame);
                            av_packet_unref(pkt);
                            goto done;
                        }
                        frameIdx++;
                        av_frame_unref(yuvFrame);
                    }
                }
            }
            av_packet_unref(pkt);
        }
    }

done:
    if (swsCtx)   sws_freeContext(swsCtx);
    if (pkt)      av_packet_free(&pkt);
    if (rgbBuf)   av_free(rgbBuf);
    if (rgbFrame) av_frame_free(&rgbFrame);
    if (yuvFrame) av_frame_free(&yuvFrame);
    if (decCtx)   avcodec_free_context(&decCtx);
    if (fmtCtx)   avformat_close_input(&fmtCtx);
    return result;
}

// =============================================================================
// MBCanvas — custom widget: video frame + MB grid + painted selection.
// Left-drag  = select MBs.   Right-drag = erase selection.
// =============================================================================
class MBCanvas : public QWidget {
    Q_OBJECT
public:
    explicit MBCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(240, 135);
        setStyleSheet("background: #111;");
    }

    void setFrame(const QImage& img, int mbCols, int mbRows)
    {
        m_frame  = img;
        m_mbCols = mbCols;
        m_mbRows = mbRows;
        update();
    }

    void loadSelection(const QSet<int>& sel) { m_selection = sel; update(); }
    const QSet<int>& selection() const { return m_selection; }
    void clearSelection() { m_selection.clear(); update(); emit selectionChanged(m_selection); }
    void setBrushSize(int s) { m_brushSize = qBound(1, s, 16); }

signals:
    void selectionChanged(const QSet<int>& sel);

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x11, 0x11, 0x11));

        if (m_frame.isNull() || m_mbCols <= 0 || m_mbRows <= 0) {
            p.setPen(QColor(60, 60, 60));
            p.setFont(QFont("Consolas", 10));
            p.drawText(rect(), Qt::AlignCenter, "Import a video to\nenable MB editing");
            return;
        }

        QRectF ir = imageRect();
        p.drawImage(ir, m_frame);

        double cellW = ir.width()  / m_mbCols;
        double cellH = ir.height() / m_mbRows;

        // Selected MB highlights
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 200, 0, 90));
        for (int mbIdx : m_selection) {
            int col = mbIdx % m_mbCols;
            int row = mbIdx / m_mbCols;
            if (col >= m_mbCols || row >= m_mbRows) continue;
            p.drawRect(QRectF(ir.x() + col * cellW, ir.y() + row * cellH, cellW, cellH));
        }

        // Grid lines
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 40), 0.5));
        for (int c = 0; c <= m_mbCols; c++) {
            double x = ir.x() + c * cellW;
            p.drawLine(QPointF(x, ir.y()), QPointF(x, ir.y() + ir.height()));
        }
        for (int r = 0; r <= m_mbRows; r++) {
            double y = ir.y() + r * cellH;
            p.drawLine(QPointF(ir.x(), y), QPointF(ir.x() + ir.width(), y));
        }

        // Hover brush preview
        if (rect().contains(m_cursorPos)) {
            QRectF br = brushRect(m_cursorPos.toPointF());
            if (br.isValid()) {
                p.setPen(QPen(QColor(255, 200, 0, 160), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRect(br);
            }
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_frame.isNull()) return;
        m_painting = true;
        m_erasing  = (e->button() == Qt::RightButton);
        applyBrush(e->position(), !m_erasing);
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        m_cursorPos = e->position().toPoint();
        if (m_painting) applyBrush(e->position(), !m_erasing);
        else            update();
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent* e) override { m_painting = false; e->accept(); }

    void leaveEvent(QEvent* e) override
    {
        m_cursorPos = QPoint(-1, -1);
        update();
        QWidget::leaveEvent(e);
    }

private:
    QRectF imageRect() const
    {
        if (m_frame.isNull()) return {};
        double scaleX = (double)width()  / m_frame.width();
        double scaleY = (double)height() / m_frame.height();
        double scale  = qMin(scaleX, scaleY);
        double drawW  = m_frame.width()  * scale;
        double drawH  = m_frame.height() * scale;
        return QRectF((width()  - drawW) / 2.0,
                      (height() - drawH) / 2.0, drawW, drawH);
    }

    QRectF brushRect(const QPointF& pos) const
    {
        QRectF ir = imageRect();
        if (!ir.contains(pos) || m_mbCols <= 0 || m_mbRows <= 0) return {};
        double cellW = ir.width()  / m_mbCols;
        double cellH = ir.height() / m_mbRows;
        int cc = qBound(0, (int)((pos.x() - ir.x()) / cellW), m_mbCols - 1);
        int cr = qBound(0, (int)((pos.y() - ir.y()) / cellH), m_mbRows - 1);
        int half = m_brushSize / 2;
        int c0 = qMax(0, cc - half),  c1 = qMin(m_mbCols - 1, cc + half);
        int r0 = qMax(0, cr - half),  r1 = qMin(m_mbRows - 1, cr + half);
        return QRectF(ir.x() + c0 * cellW, ir.y() + r0 * cellH,
                      (c1 - c0 + 1) * cellW, (r1 - r0 + 1) * cellH);
    }

    void applyBrush(const QPointF& pos, bool select)
    {
        QRectF ir = imageRect();
        if (!ir.contains(pos) || m_mbCols <= 0 || m_mbRows <= 0) return;
        double cellW = ir.width()  / m_mbCols;
        double cellH = ir.height() / m_mbRows;
        int cc = qBound(0, (int)((pos.x() - ir.x()) / cellW), m_mbCols - 1);
        int cr = qBound(0, (int)((pos.y() - ir.y()) / cellH), m_mbRows - 1);
        int half = m_brushSize / 2;
        bool changed = false;
        for (int dr = -half; dr <= half; dr++) {
            for (int dc = -half; dc <= half; dc++) {
                int col = cc + dc, row = cr + dr;
                if (col < 0 || col >= m_mbCols || row < 0 || row >= m_mbRows) continue;
                int idx = row * m_mbCols + col;
                if (select) { if (!m_selection.contains(idx)) { m_selection.insert(idx); changed = true; } }
                else        { if ( m_selection.contains(idx)) { m_selection.remove(idx); changed = true; } }
            }
        }
        if (changed) { update(); emit selectionChanged(m_selection); }
    }

    QImage    m_frame;
    QSet<int> m_selection;
    int       m_mbCols    = 0;
    int       m_mbRows    = 0;
    int       m_brushSize = 1;
    bool      m_painting  = false;
    bool      m_erasing   = false;
    QPoint    m_cursorPos = {-1, -1};
};

// =============================================================================
// Style constants
// =============================================================================

static const QString kDarkBtn =
    "QPushButton { background:#222; color:#ccc; border:1px solid #555; "
    "border-radius:3px; font:9pt 'Consolas'; padding:3px 8px; }"
    "QPushButton:hover { background:#2e2e2e; }"
    "QPushButton:disabled { color:#444; border-color:#333; }";

static const QString kDialStyle = "QDial { background:#1a1a1a; }";

static const QString kSpinStyle =
    "QSpinBox { background:#1a1a1a; color:#ccc; border:1px solid #444; "
    "font:7pt 'Consolas'; }"
    "QSpinBox:disabled { color:#444; }";

// =============================================================================
// MacroblockWidget implementation
// =============================================================================

MacroblockWidget::MacroblockWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(300);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Canvas ──────────────────────────────────────────────────────────────
    m_canvas = new MBCanvas(this);
    root->addWidget(m_canvas, 1);
    connect(m_canvas, &MBCanvas::selectionChanged,
            this, &MacroblockWidget::onMBSelectionChanged);

    // ── Navigation row ───────────────────────────────────────────────────────
    auto* navRow = new QHBoxLayout;
    m_btnPrev  = new QPushButton("\u276E", this);
    m_btnNext  = new QPushButton("\u276F", this);
    m_navLabel = new QLabel("No video loaded", this);
    m_navLabel->setAlignment(Qt::AlignCenter);
    m_navLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
    m_btnPrev->setFixedSize(28, 28);
    m_btnNext->setFixedSize(28, 28);
    m_btnPrev->setStyleSheet(kDarkBtn);
    m_btnNext->setStyleSheet(kDarkBtn);
    m_btnPrev->setEnabled(false);
    m_btnNext->setEnabled(false);
    navRow->addWidget(m_btnPrev);
    navRow->addWidget(m_navLabel, 1);
    navRow->addWidget(m_btnNext);
    root->addLayout(navRow);

    connect(m_btnPrev, &QPushButton::clicked, this, &MacroblockWidget::onPrev);
    connect(m_btnNext, &QPushButton::clicked, this, &MacroblockWidget::onNext);

    // ── Knob helper lambda ────────────────────────────────────────────────────
    // Creates a vertical label/dial/spinbox stack.  Registers in m_allDials /
    // m_allSpinboxes for bulk enable/disable and signal-blocking during load.
    auto makeKnob = [this](const QString& label, int lo, int hi,
                           QDial*& dialOut, QSpinBox*& sbOut,
                           QWidget* parent) -> QWidget*
    {
        auto* grp = new QWidget(parent);
        auto* vb  = new QVBoxLayout(grp);
        vb->setContentsMargins(2, 2, 2, 2);
        vb->setSpacing(2);

        auto* lbl = new QLabel(label, grp);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("color:#777; font:7pt 'Consolas';");

        dialOut = new QDial(grp);
        dialOut->setRange(lo, hi);
        dialOut->setValue(0);
        dialOut->setFixedSize(46, 46);
        dialOut->setWrapping(false);
        dialOut->setNotchesVisible(true);
        dialOut->setStyleSheet(kDialStyle);
        dialOut->setEnabled(false);

        sbOut = new QSpinBox(grp);
        sbOut->setRange(lo, hi);
        sbOut->setValue(0);
        sbOut->setFixedHeight(20);
        sbOut->setStyleSheet(kSpinStyle);
        sbOut->setEnabled(false);

        // Bidirectional dial ↔ spinbox sync (blocking to prevent feedback loops)
        connect(dialOut, &QDial::valueChanged, sbOut, [sbOut](int v) {
            sbOut->blockSignals(true);
            sbOut->setValue(v);
            sbOut->blockSignals(false);
        });
        connect(sbOut, QOverload<int>::of(&QSpinBox::valueChanged), dialOut,
                [dialOut](int v) {
            dialOut->blockSignals(true);
            dialOut->setValue(v);
            dialOut->blockSignals(false);
        });

        auto* center = new QHBoxLayout;
        center->addStretch(1);
        center->addWidget(dialOut);
        center->addStretch(1);
        vb->addWidget(lbl);
        vb->addLayout(center);
        vb->addWidget(sbOut);

        m_allDials.append(dialOut);
        m_allSpinboxes.append(sbOut);
        return grp;
    };

    // ── Group separator helper ─────────────────────────────────────────────────
    auto makeSep = [](const QString& title, QWidget* parent) -> QWidget* {
        auto* w  = new QWidget(parent);
        auto* hb = new QHBoxLayout(w);
        hb->setContentsMargins(2, 6, 2, 0);
        auto* lbl = new QLabel(title, w);
        lbl->setStyleSheet(
            "QLabel { color:#555; font:bold 7pt 'Consolas'; "
            "border-bottom:1px solid #2e2e2e; padding-bottom:2px; }");
        hb->addWidget(lbl);
        return w;
    };

    // ── Knob container (scrollable) ────────────────────────────────────────────
    auto* knobContainer = new QWidget;
    knobContainer->setStyleSheet("background:#111;");
    auto* kvb = new QVBoxLayout(knobContainer);
    kvb->setContentsMargins(4, 4, 4, 8);
    kvb->setSpacing(2);

    // QUANTIZATION
    kvb->addWidget(makeSep("\u2500\u2500 QUANTIZATION", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("QP \u0394", -51, 51, m_dialQP, m_sbQP, knobContainer));
        row->addStretch(1);
        kvb->addLayout(row);
    }

    // TEMPORAL / REFERENCE
    kvb->addWidget(makeSep("\u2500\u2500 TEMPORAL / REFERENCE", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("Ref Depth",  0, 7,    m_dialRef,   m_sbRef,   knobContainer));
        row->addWidget(makeKnob("Ghost Blend",0, 100,  m_dialGhost, m_sbGhost, knobContainer));
        row->addWidget(makeKnob("MV\u2192X", -128, 128,m_dialMVX,  m_sbMVX,   knobContainer));
        row->addWidget(makeKnob("MV\u2192Y", -128, 128,m_dialMVY,  m_sbMVY,   knobContainer));
        kvb->addLayout(row);
    }

    // LUMA CORRUPTION
    kvb->addWidget(makeSep("\u2500\u2500 LUMA CORRUPTION", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("Noise",     0, 255,   m_dialNoise,  m_sbNoise,  knobContainer));
        row->addWidget(makeKnob("Px Offset",-128, 127, m_dialPxOff,  m_sbPxOff,  knobContainer));
        row->addWidget(makeKnob("Invert %",  0, 100,   m_dialInvert, m_sbInvert, knobContainer));
        row->addStretch(1);
        kvb->addLayout(row);
    }

    // CHROMA CORRUPTION
    kvb->addWidget(makeSep("\u2500\u2500 CHROMA CORRUPTION", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("Chr\u2192X", -128, 128, m_dialChrX,   m_sbChrX,   knobContainer));
        row->addWidget(makeKnob("Chr\u2192Y", -128, 128, m_dialChrY,   m_sbChrY,   knobContainer));
        row->addWidget(makeKnob("Chr Offset",-128, 127,  m_dialChrOff, m_sbChrOff, knobContainer));
        row->addStretch(1);
        kvb->addLayout(row);
    }

    // SPATIAL INFLUENCE
    kvb->addWidget(makeSep("\u2500\u2500 SPATIAL INFLUENCE", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("Spill Out",   0, 8, m_dialSpill,        m_sbSpill,        knobContainer));
        row->addWidget(makeKnob("Sample In",   0, 8, m_dialSampleRadius, m_sbSampleRadius, knobContainer));
        row->addStretch(1);
        kvb->addLayout(row);
        // Tooltip hint via label is enough; QDial doesn't show tooltips well
    }

    // AMPLIFY
    kvb->addWidget(makeSep("\u2500\u2500 MV AMPLIFY", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("MV Amplify", 1, 16, m_dialMVAmp, m_sbMVAmp, knobContainer));
        row->addStretch(1);
        kvb->addLayout(row);
    }

    // PIXEL MANIPULATION
    kvb->addWidget(makeSep("\u2500\u2500 PIXEL MANIPULATION", knobContainer));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(makeKnob("Flatten %",   0, 100, m_dialBlockFlatten, m_sbBlockFlatten, knobContainer));
        row->addWidget(makeKnob("Scatter px",  0,  32, m_dialRefScatter,   m_sbRefScatter,   knobContainer));
        row->addWidget(makeKnob("Twist U",  -127, 127, m_dialColorTwistU,  m_sbColorTwistU,  knobContainer));
        row->addWidget(makeKnob("Twist V",  -127, 127, m_dialColorTwistV,  m_sbColorTwistV,  knobContainer));
        row->addStretch(1);
        kvb->addLayout(row);
    }

    kvb->addStretch(1);

    // ── TRANSIENT ENVELOPE — prominent sliders above the knob scroll area ───
    // Controls how effects smoothly return to zero over time on subsequent frames.
    // Length: how many frames the effect persists (= cascadeLen).
    // Decay:  shape of the fade-out curve (= cascadeDecay).  0=flat sustained,
    //         100=exponential halving each frame (sharp initial drop).
    {
        auto* transPanel = new QWidget(this);
        transPanel->setStyleSheet("background:#141414; border-top:1px solid #2a2a2a;");
        auto* tv = new QVBoxLayout(transPanel);
        tv->setContentsMargins(6, 4, 6, 4);
        tv->setSpacing(4);

        auto* hdr = new QLabel("\u2500\u2500 TRANSIENT ENVELOPE", transPanel);
        hdr->setStyleSheet("color:#44ffaa; font:bold 7pt 'Consolas'; border:none;");
        tv->addWidget(hdr);

        // Helper: one slider row
        auto makeSliderRow = [&](const QString& lbl, int lo, int hi,
                                 QSlider*& slOut, QLabel*& lblOut,
                                 const QString& suffix, QWidget* parent) {
            auto* row = new QHBoxLayout;
            auto* nameL = new QLabel(lbl, parent);
            nameL->setStyleSheet("color:#888; font:7pt 'Consolas'; min-width:65px;");
            nameL->setFixedWidth(65);
            row->addWidget(nameL);

            slOut = new QSlider(Qt::Horizontal, parent);
            slOut->setRange(lo, hi);
            slOut->setValue(lo);
            slOut->setEnabled(false);
            slOut->setStyleSheet(
                "QSlider::groove:horizontal { background:#222; height:6px; "
                "  border-radius:3px; }"
                "QSlider::handle:horizontal { background:#44ffaa; width:14px; "
                "  height:14px; margin:-4px 0; border-radius:7px; }"
                "QSlider::sub-page:horizontal { background:#44ffaa; "
                "  border-radius:3px; }"
                "QSlider:disabled::handle:horizontal { background:#444; }"
                "QSlider:disabled::sub-page:horizontal { background:#2a2a2a; }");
            row->addWidget(slOut, 1);

            lblOut = new QLabel(QString::number(lo) + suffix, parent);
            lblOut->setStyleSheet("color:#44ffaa; font:bold 7pt 'Consolas'; min-width:60px;");
            lblOut->setFixedWidth(60);
            row->addWidget(lblOut);
            tv->addLayout(row);
        };

        makeSliderRow("Length",     0, 60,  m_sliderTransLen,   m_lblTransLen,
                      " frames", transPanel);
        makeSliderRow("Decay",      0, 100, m_sliderTransDecay, m_lblTransDecay,
                      "%",       transPanel);

        root->addWidget(transPanel);
    }

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(knobContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMaximumHeight(290);
    scrollArea->setMinimumHeight(110);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(
        "QScrollArea { background:#111; border:none; }"
        "QScrollBar:vertical { background:#1a1a1a; width:8px; border:none; }"
        "QScrollBar::handle:vertical { background:#444; border-radius:4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    root->addWidget(scrollArea);

    // ── Connect all knobs (dial + spinbox) to m_edits via member pointer ────────
    // Both dial AND spinbox trigger the same field update; the sync lambdas inside
    // makeKnob use blockSignals so only one of the two fires per user interaction.
    auto connectKnobs = [this](QDial* d, QSpinBox* sb, int FrameMBParams::* field) {
        auto update = [this, field](int v) { m_edits[m_currentFrame].*field = v; };
        connect(d,  &QDial::valueChanged,
                this, update);
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
                this, update);
    };

    connectKnobs(m_dialQP,     m_sbQP,     &FrameMBParams::qpDelta);
    connectKnobs(m_dialRef,    m_sbRef,    &FrameMBParams::refDepth);
    connectKnobs(m_dialGhost,  m_sbGhost,  &FrameMBParams::ghostBlend);
    connectKnobs(m_dialMVX,    m_sbMVX,    &FrameMBParams::mvDriftX);
    connectKnobs(m_dialMVY,    m_sbMVY,    &FrameMBParams::mvDriftY);
    connectKnobs(m_dialNoise,  m_sbNoise,  &FrameMBParams::noiseLevel);
    connectKnobs(m_dialPxOff,  m_sbPxOff,  &FrameMBParams::pixelOffset);
    connectKnobs(m_dialInvert, m_sbInvert, &FrameMBParams::invertLuma);
    connectKnobs(m_dialChrX,   m_sbChrX,   &FrameMBParams::chromaDriftX);
    connectKnobs(m_dialChrY,   m_sbChrY,   &FrameMBParams::chromaDriftY);
    connectKnobs(m_dialChrOff, m_sbChrOff, &FrameMBParams::chromaOffset);
    connectKnobs(m_dialSpill,        m_sbSpill,        &FrameMBParams::spillRadius);
    connectKnobs(m_dialSampleRadius, m_sbSampleRadius, &FrameMBParams::sampleRadius);
    connectKnobs(m_dialMVAmp, m_sbMVAmp, &FrameMBParams::mvAmplify);
    connectKnobs(m_dialBlockFlatten, m_sbBlockFlatten, &FrameMBParams::blockFlatten);
    connectKnobs(m_dialRefScatter,   m_sbRefScatter,   &FrameMBParams::refScatter);
    connectKnobs(m_dialColorTwistU,  m_sbColorTwistU,  &FrameMBParams::colorTwistU);
    connectKnobs(m_dialColorTwistV,  m_sbColorTwistV,  &FrameMBParams::colorTwistV);

    // ── Transient slider connections ──────────────────────────────────────────
    connect(m_sliderTransLen, &QSlider::valueChanged, this, [this](int v) {
        m_edits[m_currentFrame].cascadeLen = v;
        m_lblTransLen->setText(QString::number(v) + " frames");
    });
    connect(m_sliderTransDecay, &QSlider::valueChanged, this, [this](int v) {
        m_edits[m_currentFrame].cascadeDecay = v;
        // Show the shape description
        QString desc;
        if      (v == 0)   desc = "0% (flat)";
        else if (v < 25)   desc = QString::number(v) + "% (gentle)";
        else if (v < 60)   desc = QString::number(v) + "% (medium)";
        else if (v < 90)   desc = QString::number(v) + "% (sharp)";
        else               desc = QString::number(v) + "% (fast)";
        m_lblTransDecay->setText(desc);
    });

    // ── Brush size + Clear controls ────────────────────────────────────────────
    auto* ctrlRow = new QHBoxLayout;
    ctrlRow->setSpacing(6);

    auto* brushLabel = new QLabel("Brush:", this);
    brushLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
    m_sbBrush = new QSpinBox(this);
    m_sbBrush->setRange(1, 16);
    m_sbBrush->setValue(1);
    m_sbBrush->setFixedWidth(50);
    m_sbBrush->setStyleSheet(
        "QSpinBox { background:#1a1a1a; color:#ccc; border:1px solid #444; "
        "font:8pt 'Consolas'; }");
    m_sbBrush->setEnabled(false);

    m_btnClearFrame = new QPushButton("Clear Frame", this);
    m_btnClearAll   = new QPushButton("Clear All",   this);
    m_btnClearFrame->setStyleSheet(kDarkBtn);
    m_btnClearAll  ->setStyleSheet(kDarkBtn);
    m_btnClearFrame->setEnabled(false);
    m_btnClearAll  ->setEnabled(false);

    ctrlRow->addWidget(brushLabel);
    ctrlRow->addWidget(m_sbBrush);
    ctrlRow->addStretch(1);
    ctrlRow->addWidget(m_btnClearFrame);
    ctrlRow->addWidget(m_btnClearAll);
    root->addLayout(ctrlRow);

    connect(m_sbBrush, QOverload<int>::of(&QSpinBox::valueChanged),
            m_canvas, &MBCanvas::setBrushSize);
    connect(m_btnClearFrame, &QPushButton::clicked,
            this, &MacroblockWidget::onClearFrame);
    connect(m_btnClearAll, &QPushButton::clicked,
            this, &MacroblockWidget::clearAllEdits);

    // Async decode watcher
    m_watcher = new QFutureWatcher<QImage>(this);
    connect(m_watcher, &QFutureWatcher<QImage>::finished,
            this, &MacroblockWidget::onFrameDecoded);
}

MacroblockWidget::~MacroblockWidget()
{
    m_watcher->cancel();
    m_watcher->waitForFinished();
}

// =============================================================================
// Public interface
// =============================================================================

void MacroblockWidget::setVideo(const QString& videoPath,
                                 const AnalysisReport& report)
{
    m_watcher->cancel();
    m_watcher->waitForFinished();

    m_videoPath    = videoPath;
    m_totalFrames  = report.frames.size();
    m_mbCols       = report.sps.mb_width;
    m_mbRows       = report.sps.mb_height;
    m_frameTypes.clear();
    m_edits.clear();
    m_currentFrame = 0;

    for (const FrameInfo& f : report.frames)
        m_frameTypes.append(f.pictType == '\0' ? '?' : f.pictType);

    if (m_mbCols <= 0 && report.sps.frame_width_px > 0) {
        m_mbCols = (report.sps.frame_width_px  + 15) / 16;
        m_mbRows = (report.sps.frame_height_px + 15) / 16;
    }

    if (m_totalFrames > 0 && m_mbCols > 0) {
        setControlsEnabled(true);
        navigateTo(0);
    } else {
        setControlsEnabled(false);
        m_canvas->setFrame(QImage(), 0, 0);
        m_navLabel->setText("No video loaded");
    }
}

void MacroblockWidget::reload(const QString& videoPath,
                               const AnalysisReport& report)
{
    // Update metadata WITHOUT clearing m_edits — preserve across transforms.
    int savedFrame = m_currentFrame;
    m_watcher->cancel();
    m_watcher->waitForFinished();

    m_videoPath   = videoPath;
    m_totalFrames = report.frames.size();
    m_mbCols      = report.sps.mb_width;
    m_mbRows      = report.sps.mb_height;
    m_frameTypes.clear();
    for (const FrameInfo& f : report.frames)
        m_frameTypes.append(f.pictType == '\0' ? '?' : f.pictType);

    if (m_mbCols <= 0 && report.sps.frame_width_px > 0) {
        m_mbCols = (report.sps.frame_width_px  + 15) / 16;
        m_mbRows = (report.sps.frame_height_px + 15) / 16;
    }

    if (m_totalFrames > 0 && m_mbCols > 0) {
        setControlsEnabled(true);
        navigateTo(qBound(0, savedFrame, m_totalFrames - 1));
    } else {
        setControlsEnabled(false);
        m_canvas->setFrame(QImage(), 0, 0);
        m_navLabel->setText("No video loaded");
    }
}

void MacroblockWidget::clearAllEdits()
{
    m_edits.clear();
    m_canvas->loadSelection(QSet<int>());
    loadKnobsFromCurrentFrame();
}

void MacroblockWidget::navigateToFrame(int frameIdx)
{
    if (frameIdx == m_currentFrame) return;  // already there — no-op prevents loops
    navigateTo(frameIdx);
    // Intentionally does NOT emit frameNavigated (called from MainWindow on timeline change)
}

// =============================================================================
// Navigation
// =============================================================================

void MacroblockWidget::navigateTo(int frameIdx)
{
    if (m_totalFrames <= 0) return;
    m_currentFrame = qBound(0, frameIdx, m_totalFrames - 1);

    updateNavLabel();
    loadKnobsFromCurrentFrame();
    m_canvas->loadSelection(m_edits.value(m_currentFrame).selectedMBs);

    m_watcher->cancel();
    QString path = m_videoPath;
    int     idx  = m_currentFrame;
    m_watcher->setFuture(QtConcurrent::run([path, idx]() {
        return decodeFrameAt(path, idx);
    }));
}

void MacroblockWidget::onFrameDecoded()
{
    if (m_watcher->isCanceled()) return;
    m_canvas->setFrame(m_watcher->result(), m_mbCols, m_mbRows);
}

void MacroblockWidget::onPrev()
{
    if (m_currentFrame <= 0) return;
    navigateTo(m_currentFrame - 1);
    emit frameNavigated(m_currentFrame);
}

void MacroblockWidget::onNext()
{
    if (m_currentFrame >= m_totalFrames - 1) return;
    navigateTo(m_currentFrame + 1);
    emit frameNavigated(m_currentFrame);
}

// =============================================================================
// Knob ↔ edit-map sync
// =============================================================================

void MacroblockWidget::loadKnobsFromCurrentFrame()
{
    const FrameMBParams& p = m_edits.value(m_currentFrame);

    // Block all signals while we push values so the update lambdas don't fire
    for (QDial*    d : m_allDials)     d->blockSignals(true);
    for (QSpinBox* s : m_allSpinboxes) s->blockSignals(true);

    auto setKnob = [](QDial* d, QSpinBox* sb, int v) {
        d->setValue(v);
        sb->setValue(v);
    };

    setKnob(m_dialQP,     m_sbQP,     p.qpDelta);
    setKnob(m_dialRef,    m_sbRef,    p.refDepth);
    setKnob(m_dialGhost,  m_sbGhost,  p.ghostBlend);
    setKnob(m_dialMVX,    m_sbMVX,    p.mvDriftX);
    setKnob(m_dialMVY,    m_sbMVY,    p.mvDriftY);
    setKnob(m_dialNoise,  m_sbNoise,  p.noiseLevel);
    setKnob(m_dialPxOff,  m_sbPxOff,  p.pixelOffset);
    setKnob(m_dialInvert, m_sbInvert, p.invertLuma);
    setKnob(m_dialChrX,   m_sbChrX,   p.chromaDriftX);
    setKnob(m_dialChrY,   m_sbChrY,   p.chromaDriftY);
    setKnob(m_dialChrOff, m_sbChrOff, p.chromaOffset);
    setKnob(m_dialSpill,        m_sbSpill,        p.spillRadius);
    setKnob(m_dialSampleRadius, m_sbSampleRadius, p.sampleRadius);
    setKnob(m_dialMVAmp,        m_sbMVAmp,        p.mvAmplify);

    // Sync transient sliders (block signals so the update lambdas don't write back)
    m_sliderTransLen->blockSignals(true);
    m_sliderTransDecay->blockSignals(true);
    m_sliderTransLen->setValue(p.cascadeLen);
    m_sliderTransDecay->setValue(p.cascadeDecay);
    m_lblTransLen->setText(QString::number(p.cascadeLen) + " frames");
    {
        int v = p.cascadeDecay;
        QString desc;
        if      (v == 0) desc = "0% (flat)";
        else if (v < 25) desc = QString::number(v) + "% (gentle)";
        else if (v < 60) desc = QString::number(v) + "% (medium)";
        else if (v < 90) desc = QString::number(v) + "% (sharp)";
        else             desc = QString::number(v) + "% (fast)";
        m_lblTransDecay->setText(desc);
    }
    m_sliderTransLen->blockSignals(false);
    m_sliderTransDecay->blockSignals(false);
    setKnob(m_dialBlockFlatten, m_sbBlockFlatten, p.blockFlatten);
    setKnob(m_dialRefScatter,   m_sbRefScatter,   p.refScatter);
    setKnob(m_dialColorTwistU,  m_sbColorTwistU,  p.colorTwistU);
    setKnob(m_dialColorTwistV,  m_sbColorTwistV,  p.colorTwistV);

    for (QDial*    d : m_allDials)     d->blockSignals(false);
    for (QSpinBox* s : m_allSpinboxes) s->blockSignals(false);
}

void MacroblockWidget::onClearFrame()
{
    m_edits.remove(m_currentFrame);
    loadKnobsFromCurrentFrame();
    m_canvas->loadSelection(QSet<int>());
}

QSet<int> MacroblockWidget::currentSelection() const
{
    return m_canvas->selection();
}

void MacroblockWidget::onMBSelectionChanged(const QSet<int>& sel)
{
    m_edits[m_currentFrame].selectedMBs = sel;
    emit mbSelectionChanged(sel);
}

// =============================================================================
// Helpers
// =============================================================================

void MacroblockWidget::updateNavLabel()
{
    char type = (m_currentFrame < m_frameTypes.size())
                ? m_frameTypes[m_currentFrame] : '?';
    QString typeStr;
    switch (type) {
    case 'I': typeStr = "<font color='#ffffff'>I</font>"; break;
    case 'P': typeStr = "<font color='#4488ff'>P</font>"; break;
    case 'B': typeStr = "<font color='#ff64b4'>B</font>"; break;
    default:  typeStr = "<font color='#888'>?</font>";    break;
    }
    m_navLabel->setText(
        QString("<font color='#aaa' style='font-family:Consolas;font-size:9pt;'>"
                "%1 / %2 \u2014 %3</font>")
        .arg(m_currentFrame).arg(m_totalFrames - 1).arg(typeStr));
}

void MacroblockWidget::setControlsEnabled(bool enabled)
{
    m_btnPrev        ->setEnabled(enabled);
    m_btnNext        ->setEnabled(enabled);
    m_sbBrush        ->setEnabled(enabled);
    m_btnClearFrame  ->setEnabled(enabled);
    m_btnClearAll    ->setEnabled(enabled);
    m_sliderTransLen ->setEnabled(enabled);
    m_sliderTransDecay->setEnabled(enabled);
    for (QDial*    d : m_allDials)     d->setEnabled(enabled);
    for (QSpinBox* s : m_allSpinboxes) s->setEnabled(enabled);
}

#include "MacroblockWidget.moc"
