#include "MacroblockWidget.h"

#include "core/logger/ControlLogger.h"
#include "core/presets/PresetManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QDial>
#include <QSlider>
#include <QSpinBox>
#include <QGroupBox>
#include <QComboBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
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
// NoScrollSpinBox — QSpinBox that ignores scroll-wheel events.
// Forces the user to click inside and type intentionally.
// =============================================================================
class NoScrollSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

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
    void setDeselectMode(bool on) { m_deselectMode = on; }
    bool deselectMode() const { return m_deselectMode; }

signals:
    void selectionChanged(const QSet<int>& sel);
    void panRequested(QPoint delta);

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
        if (e->button() == Qt::MiddleButton) {
            m_panning = true;
            m_panLastPos = e->globalPosition().toPoint();
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
        if (m_frame.isNull()) return;
        m_painting = true;
        // Right-button always erases; left-button uses the mode toggle,
        // but Alt temporarily inverts the mode.
        bool rightBtn = (e->button() == Qt::RightButton);
        bool altHeld  = (e->modifiers() & Qt::AltModifier);
        m_erasing = rightBtn || (m_deselectMode != altHeld);
        applyBrush(e->position(), !m_erasing);
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_panning) {
            QPoint delta = e->globalPosition().toPoint() - m_panLastPos;
            m_panLastPos = e->globalPosition().toPoint();
            emit panRequested(delta);
            e->accept();
            return;
        }
        m_cursorPos = e->position().toPoint();
        if (m_painting) applyBrush(e->position(), !m_erasing);
        else            update();
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::MiddleButton && m_panning) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
        }
        m_painting = false;
        e->accept();
    }

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
    int       m_mbCols      = 0;
    int       m_mbRows      = 0;
    int       m_brushSize   = 1;
    bool      m_painting    = false;
    bool      m_erasing     = false;
    bool      m_deselectMode = false;
    bool      m_panning     = false;
    QPoint    m_panLastPos;
    QPoint    m_cursorPos   = {-1, -1};
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

    // ── Canvas ───────────────────────────────────────────────────────────────
    m_canvas = new MBCanvas(this);
    connect(m_canvas, &MBCanvas::selectionChanged,
            this, &MacroblockWidget::onMBSelectionChanged);
    connect(m_canvas, &MBCanvas::panRequested, this, [this](QPoint delta) {
        auto* hb = m_canvasScroll->horizontalScrollBar();
        auto* vb = m_canvasScroll->verticalScrollBar();
        hb->setValue(hb->value() - delta.x());
        vb->setValue(vb->value() - delta.y());
    });

    m_canvasScroll = new QScrollArea(this);
    m_canvasScroll->setWidget(m_canvas);
    m_canvasScroll->setWidgetResizable(false);  // we manage canvas size directly
    m_canvasScroll->setStyleSheet(
        "QScrollArea { border:none; background:#111; }"
        "QScrollBar:vertical   { background:#1a1a1a; width:8px;  border:none; }"
        "QScrollBar:horizontal { background:#1a1a1a; height:8px; border:none; }"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal "
        "  { background:#333; border-radius:4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal { width:0; height:0; }");
    // eventFilter: viewport resize → sync canvas size; canvas wheel → zoom
    m_canvasScroll->viewport()->installEventFilter(this);
    m_canvas->installEventFilter(this);

    // ── Inner splitter: canvas (top, draggable) | controls (bottom) ──────────
    m_innerSplitter = new QSplitter(Qt::Vertical, this);
    m_innerSplitter->setChildrenCollapsible(false);
    m_innerSplitter->setHandleWidth(5);
    m_innerSplitter->setStyleSheet(
        "QSplitter::handle:vertical { background:#2a2a2a; border-top:1px solid #333; }");

    auto* controlsWidget = new QWidget(this);
    controlsWidget->setMinimumHeight(80);
    m_controlsLayout = new QVBoxLayout(controlsWidget);
    m_controlsLayout->setContentsMargins(0, 4, 0, 0);
    m_controlsLayout->setSpacing(4);

    m_innerSplitter->addWidget(m_canvasScroll);
    m_innerSplitter->addWidget(controlsWidget);
    m_innerSplitter->setStretchFactor(0, 3);
    m_innerSplitter->setStretchFactor(1, 1);
    root->addWidget(m_innerSplitter, 1);

    // ── Navigation bar (wrapped in QWidget for pop-out reparenting) ──────────
    m_navBar = new QWidget(this);
    m_navBar->setStyleSheet("background:transparent;");
    {
        auto* navRow = new QHBoxLayout(m_navBar);
        navRow->setContentsMargins(0, 0, 0, 0);
        navRow->setSpacing(4);
        m_btnPrev  = new QPushButton("\u276E", m_navBar);
        m_btnNext  = new QPushButton("\u276F", m_navBar);
        m_navLabel = new QLabel("No video loaded", m_navBar);
        m_navLabel->setAlignment(Qt::AlignCenter);
        m_navLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
        m_btnPrev->setFixedSize(28, 28);
        m_btnNext->setFixedSize(28, 28);
        m_btnPrev->setStyleSheet(kDarkBtn);
        m_btnNext->setStyleSheet(kDarkBtn);
        m_btnPrev->setEnabled(false);
        m_btnNext->setEnabled(false);
        m_btnPopOutCanvas = new QPushButton("\u2922", m_navBar);  // ⤢
        m_btnPopOutCanvas->setFixedSize(28, 28);
        m_btnPopOutCanvas->setToolTip("Pop canvas out to a floating window");
        m_btnPopOutCanvas->setStyleSheet(kDarkBtn);
        m_btnPopOutCanvas->setEnabled(false);
        navRow->addWidget(m_btnPrev);
        navRow->addWidget(m_navLabel, 1);
        navRow->addWidget(m_btnNext);
        navRow->addWidget(m_btnPopOutCanvas);
    }
    m_controlsLayout->addWidget(m_navBar);

    connect(m_btnPrev, &QPushButton::clicked, this, &MacroblockWidget::onPrev);
    connect(m_btnNext, &QPushButton::clicked, this, &MacroblockWidget::onNext);
    connect(m_btnPopOutCanvas, &QPushButton::clicked, this, [this]() {
        if (m_canvasIsPopped) {
            // ── Dock back ──────────────────────────────────────────────
            m_controlsLayout->insertWidget(0, m_navBar);
            m_navBar->show();
            m_innerSplitter->insertWidget(0, m_canvasScroll);
            m_canvasScroll->show();
            // m_brushBar goes back at the end of controlsLayout
            m_controlsLayout->addWidget(m_brushBar);
            m_brushBar->show();
            if (m_popOutWindow) { m_popOutWindow->deleteLater(); m_popOutWindow = nullptr; }
            m_btnPopOutCanvas->setText("\u2922");
            m_canvasIsPopped = false;
        } else {
            // ── Pop out ────────────────────────────────────────────────
            m_popOutWindow = new QWidget(nullptr);
            m_popOutWindow->setWindowTitle("MB Canvas \u2014 LaMoshPit");
            m_popOutWindow->setAttribute(Qt::WA_DeleteOnClose, false);
            m_popOutWindow->setStyleSheet("background:#0a0a0a;");
            m_popOutWindow->installEventFilter(this);
            auto* popLayout = new QVBoxLayout(m_popOutWindow);
            popLayout->setContentsMargins(4, 4, 4, 4);
            popLayout->setSpacing(4);
            popLayout->addWidget(m_navBar);
            popLayout->addWidget(m_canvasScroll, 1);
            popLayout->addWidget(m_brushBar);
            m_popOutWindow->resize(800, 600);
            m_popOutWindow->show();
            m_btnPopOutCanvas->setText("\u2921");  // ⤡ (dock-back icon)
            m_canvasIsPopped = true;
        }
    });

    // ── Zoom row ─────────────────────────────────────────────────────────────
    {
        auto* zoomRow = new QHBoxLayout;
        auto* zoomLbl = new QLabel("Zoom:", this);
        zoomLbl->setStyleSheet("color:#555; font:7pt 'Consolas'; background:transparent;");
        zoomLbl->setFixedWidth(42);
        zoomRow->addWidget(zoomLbl);

        m_sliderZoom = new QSlider(Qt::Horizontal, this);
        m_sliderZoom->setRange(100, 500);   // 1.0× .. 5.0× (×100)
        m_sliderZoom->setValue(100);
        m_sliderZoom->setEnabled(false);
        m_sliderZoom->setToolTip("Zoom the MB canvas. Scroll wheel on the canvas also zooms.");
        m_sliderZoom->setStyleSheet(
            "QSlider::groove:horizontal { background:#1e1e1e; height:5px; border-radius:2px; }"
            "QSlider::handle:horizontal { background:#444; width:12px; height:12px; "
            "  margin:-4px 0; border-radius:6px; }"
            "QSlider::sub-page:horizontal { background:#444; border-radius:2px; }"
            "QSlider:disabled::handle:horizontal { background:#2a2a2a; }"
            "QSlider:disabled::sub-page:horizontal { background:#1e1e1e; }");
        zoomRow->addWidget(m_sliderZoom, 1);

        m_lblZoom = new QLabel("1.0\u00d7", this);
        m_lblZoom->setStyleSheet("color:#555; font:7pt 'Consolas'; background:transparent;");
        m_lblZoom->setFixedWidth(36);
        m_lblZoom->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        zoomRow->addWidget(m_lblZoom);

        m_controlsLayout->addLayout(zoomRow);

        connect(m_sliderZoom, &QSlider::valueChanged, this, [this](int v) {
            m_zoom = v / 100.0f;
            m_lblZoom->setText(QString::number(m_zoom, 'f', 1) + "\u00d7");
            QSize vpSize = m_canvasScroll->viewport()->size();
            m_canvas->resize(QSize(qRound(vpSize.width()  * m_zoom),
                                   qRound(vpSize.height() * m_zoom)));
        });
    }

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

        sbOut = new NoScrollSpinBox(grp);
        sbOut->setRange(-99999, 99999);  // unclamped — user can type any value
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

    // ── Knob container (scrollable) ────────────────────────────────────────────
    auto* knobContainer = new QWidget;
    knobContainer->setStyleSheet("background:#111;");
    auto* kvb = new QVBoxLayout(knobContainer);
    kvb->setContentsMargins(0, 0, 0, 4);
    kvb->setSpacing(0);

    // ── Collapsible section helper ─────────────────────────────────────────────
    // Returns the content widget and its row layout.  The toggle button and
    // content widget are already added to kvb when the struct is returned.
    struct Section { QWidget* w; QHBoxLayout* l; };
    static const QString kSecBtn =
        "QPushButton { background:#171717; color:#555; font:bold 7pt 'Consolas'; "
        "  text-align:left; border:none; border-bottom:1px solid #222; padding:3px 6px; }"
        "QPushButton:hover { background:#1e1e1e; color:#888; }";

    auto makeSection = [&](const QString& title) -> Section {
        auto* btn = new QPushButton(knobContainer);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setFlat(true);
        btn->setText("\u25bc " + title);
        btn->setStyleSheet(kSecBtn);
        btn->setCursor(Qt::PointingHandCursor);
        kvb->addWidget(btn);

        auto* content = new QWidget(knobContainer);
        content->setStyleSheet("background:#111;");
        auto* row = new QHBoxLayout(content);
        row->setContentsMargins(2, 2, 2, 4);
        row->setSpacing(4);
        kvb->addWidget(content);

        QString t = title;
        connect(btn, &QPushButton::toggled, content, [btn, content, t](bool on) {
            content->setVisible(on);
            btn->setText((on ? "\u25bc " : "\u25ba ") + t);
        });
        return {content, row};
    };

    // ── QUANTIZATION ─────────────────────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 QUANTIZATION");
        s.l->addWidget(makeKnob("QP \u0394", -51, 51, m_dialQP, m_sbQP, s.w));
        s.l->addStretch(1);
    }

    // ── MOTION & TEMPORAL ────────────────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 MOTION & TEMPORAL");
        s.l->addWidget(makeKnob("Ref Depth",    0,   7,   m_dialRef,        m_sbRef,        s.w));
        s.l->addWidget(makeKnob("Ghost Blend",  0, 100,   m_dialGhost,      m_sbGhost,      s.w));
        s.l->addWidget(makeKnob("MV\u2192X",  -128, 128,  m_dialMVX,        m_sbMVX,        s.w));
        s.l->addWidget(makeKnob("MV\u2192Y",  -128, 128,  m_dialMVY,        m_sbMVY,        s.w));
        s.l->addWidget(makeKnob("MV Amplify",   1,  16,   m_dialMVAmp,      m_sbMVAmp,      s.w));
        s.l->addWidget(makeKnob("Diff Amp",     0, 200,   m_dialTempDiffAmp,m_sbTempDiffAmp,s.w));
        s.l->addStretch(1);
    }

    // ── LUMA CORRUPTION ──────────────────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 LUMA CORRUPTION");
        s.l->addWidget(makeKnob("Noise",      0,  255,  m_dialNoise,     m_sbNoise,     s.w));
        s.l->addWidget(makeKnob("Px Offset", -128, 127, m_dialPxOff,     m_sbPxOff,     s.w));
        s.l->addWidget(makeKnob("Invert %",   0,  100,  m_dialInvert,    m_sbInvert,    s.w));
        s.l->addWidget(makeKnob("Posterize",  1,    8,  m_dialPosterize, m_sbPosterize, s.w));
        s.l->addWidget(makeKnob("Sharpen",  -100, 100,  m_dialSharpen,   m_sbSharpen,   s.w));
        s.l->addStretch(1);
    }

    // ── CHROMA & COLOUR ──────────────────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 CHROMA & COLOUR");
        s.l->addWidget(makeKnob("Chr\u2192X",  -128, 128, m_dialChrX,       m_sbChrX,       s.w));
        s.l->addWidget(makeKnob("Chr\u2192Y",  -128, 128, m_dialChrY,       m_sbChrY,       s.w));
        s.l->addWidget(makeKnob("Chr Offset",  -128, 127, m_dialChrOff,     m_sbChrOff,     s.w));
        s.l->addWidget(makeKnob("Twist U",    -127, 127,  m_dialColorTwistU,m_sbColorTwistU,s.w));
        s.l->addWidget(makeKnob("Twist V",    -127, 127,  m_dialColorTwistV,m_sbColorTwistV,s.w));
        s.l->addWidget(makeKnob("Hue Rotate",    0, 359,  m_dialHueRotate,  m_sbHueRotate,  s.w));
        s.l->addStretch(1);
    }

    // ── SPATIAL & PIXEL ──────────────────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 SPATIAL & PIXEL");
        s.l->addWidget(makeKnob("Spill Out",   0,   8,  m_dialSpill,        m_sbSpill,        s.w));
        s.l->addWidget(makeKnob("Sample In",   0,   8,  m_dialSampleRadius, m_sbSampleRadius, s.w));
        s.l->addWidget(makeKnob("Flatten %",   0, 100,  m_dialBlockFlatten, m_sbBlockFlatten, s.w));
        s.l->addWidget(makeKnob("Scatter px",  0,  32,  m_dialRefScatter,   m_sbRefScatter,   s.w));
        s.l->addWidget(makeKnob("Shuffle px",  0,  32,  m_dialPixelShuffle, m_sbPixelShuffle, s.w));
        s.l->addStretch(1);
    }

    // ── BITSTREAM SURGERY — MOTION ───────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 BITSTREAM \u2014 MOTION");
        s.l->addWidget(makeKnob("MVD\u2192X",  -128, 128, m_dialBsMvdX,      m_sbBsMvdX,      s.w));
        s.l->addWidget(makeKnob("MVD\u2192Y",  -128, 128, m_dialBsMvdY,      m_sbBsMvdY,      s.w));
        s.l->addWidget(makeKnob("Force Skip",     0, 100, m_dialBsForceSkip, m_sbBsForceSkip, s.w));
        s.l->addStretch(1);
    }

    // ── BITSTREAM SURGERY — PREDICTION ───────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 BITSTREAM \u2014 PREDICTION");
        s.l->addWidget(makeKnob("Intra Mode",  -1,  3,  m_dialBsIntraMode, m_sbBsIntraMode, s.w));
        s.l->addWidget(makeKnob("MB Type",     -1,  4,  m_dialBsMbType,    m_sbBsMbType,    s.w));
        s.l->addStretch(1);
    }

    // ── BITSTREAM SURGERY — RESIDUAL ─────────────────────────────────────────
    {
        auto s = makeSection("\u2500\u2500 BITSTREAM \u2014 RESIDUAL");
        s.l->addWidget(makeKnob("DCT Scale",   0, 200,  m_dialBsDctScale,  m_sbBsDctScale,  s.w));
        s.l->addWidget(makeKnob("CBP Zero",    0, 100,  m_dialBsCbpZero,   m_sbBsCbpZero,   s.w));
        s.l->addStretch(1);
    }

    kvb->addStretch(1);

    // ── TRANSIENT ENVELOPE — collapsible panel above the knob scroll area ───
    // Length: how many frames the cascade persists (cascadeLen, max = total frames − 1).
    // Decay:  0=flat sustained, 100=sharp exponential fade.
    {
        auto* transPanel = new QWidget(this);
        transPanel->setStyleSheet("background:#141414; border-top:1px solid #2a2a2a;");
        auto* tv = new QVBoxLayout(transPanel);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(0);

        auto* transHdr = new QPushButton("\u25bc \u2500\u2500 TRANSIENT ENVELOPE", transPanel);
        transHdr->setCheckable(true);
        transHdr->setChecked(true);
        transHdr->setFlat(true);
        transHdr->setStyleSheet(
            "QPushButton { background:#141414; color:#44ffaa; font:bold 7pt 'Consolas'; "
            "  text-align:left; border:none; border-bottom:1px solid #222; padding:3px 6px; }"
            "QPushButton:hover { background:#1c1c1c; }");
        transHdr->setCursor(Qt::PointingHandCursor);
        tv->addWidget(transHdr);

        auto* transContent = new QWidget(transPanel);
        transContent->setStyleSheet("background:#141414;");
        auto* tcv = new QVBoxLayout(transContent);
        tcv->setContentsMargins(6, 4, 6, 4);
        tcv->setSpacing(4);
        tv->addWidget(transContent);

        connect(transHdr, &QPushButton::toggled, transContent,
                [transHdr, transContent](bool on) {
            transContent->setVisible(on);
            transHdr->setText((on ? "\u25bc " : "\u25ba ")
                              + QString("\u2500\u2500 TRANSIENT ENVELOPE"));
        });

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
                "QSlider::groove:horizontal { background:#222; height:6px; border-radius:3px; }"
                "QSlider::handle:horizontal { background:#44ffaa; width:14px; "
                "  height:14px; margin:-4px 0; border-radius:7px; }"
                "QSlider::sub-page:horizontal { background:#44ffaa; border-radius:3px; }"
                "QSlider:disabled::handle:horizontal { background:#444; }"
                "QSlider:disabled::sub-page:horizontal { background:#2a2a2a; }");
            row->addWidget(slOut, 1);

            lblOut = new QLabel(QString::number(lo) + suffix, parent);
            lblOut->setStyleSheet("color:#44ffaa; font:bold 7pt 'Consolas'; min-width:60px;");
            lblOut->setFixedWidth(60);
            row->addWidget(lblOut);
            tcv->addLayout(row);
        };

        makeSliderRow("Length",  0, 60,  m_sliderTransLen,   m_lblTransLen,
                      " frames", transContent);
        makeSliderRow("Decay",   0, 100, m_sliderTransDecay, m_lblTransDecay,
                      "%",       transContent);

        m_controlsLayout->addWidget(transPanel);
    }

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(knobContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(80);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(
        "QScrollArea { background:#111; border:none; }"
        "QScrollBar:vertical { background:#1a1a1a; width:8px; border:none; }"
        "QScrollBar::handle:vertical { background:#444; border-radius:4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    m_controlsLayout->addWidget(scrollArea);

    // ── Connect all knobs (dial + spinbox) to m_edits via member pointer ────────
    // Both dial AND spinbox trigger the same field update; the sync lambdas inside
    // makeKnob use blockSignals so only one of the two fires per user interaction.
    // The name string is forwarded to ControlLogger for human-readable log output.
    auto connectKnobs = [this](QDial* d, QSpinBox* sb, int FrameMBParams::* field,
                                const QString& name) {
        auto update = [this, field, name](int v) {
            // Write to every frame in the active timeline range so that a
            // multi-frame selection gets the same value without the user having
            // to navigate to each frame individually.
            const QVector<int>& range = m_activeRange.isEmpty()
                                        ? QVector<int>{m_currentFrame}
                                        : m_activeRange;
            for (int fi : range) {
                int& target = m_edits[fi].*field;
                int  oldVal = target;
                target = v;
                if (fi == m_currentFrame)
                    ControlLogger::instance().logKnobChange(name, fi, oldVal, v);
            }
        };
        connect(d,  &QDial::valueChanged,
                this, update);
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
                this, update);
    };

    connectKnobs(m_dialQP,     m_sbQP,     &FrameMBParams::qpDelta,      "QP Delta");
    connectKnobs(m_dialRef,    m_sbRef,    &FrameMBParams::refDepth,     "Ref Depth");
    connectKnobs(m_dialGhost,  m_sbGhost,  &FrameMBParams::ghostBlend,   "Ghost Blend");
    connectKnobs(m_dialMVX,    m_sbMVX,    &FrameMBParams::mvDriftX,     "MV Drift X");
    connectKnobs(m_dialMVY,    m_sbMVY,    &FrameMBParams::mvDriftY,     "MV Drift Y");
    connectKnobs(m_dialNoise,  m_sbNoise,  &FrameMBParams::noiseLevel,   "Noise Level");
    connectKnobs(m_dialPxOff,  m_sbPxOff,  &FrameMBParams::pixelOffset,  "Pixel Offset");
    connectKnobs(m_dialInvert, m_sbInvert, &FrameMBParams::invertLuma,   "Invert Luma");
    connectKnobs(m_dialChrX,   m_sbChrX,   &FrameMBParams::chromaDriftX, "Chroma Drift X");
    connectKnobs(m_dialChrY,   m_sbChrY,   &FrameMBParams::chromaDriftY, "Chroma Drift Y");
    connectKnobs(m_dialChrOff, m_sbChrOff, &FrameMBParams::chromaOffset, "Chroma Offset");
    connectKnobs(m_dialSpill,        m_sbSpill,        &FrameMBParams::spillRadius,  "Spill Radius");
    connectKnobs(m_dialSampleRadius, m_sbSampleRadius, &FrameMBParams::sampleRadius, "Sample Radius");
    connectKnobs(m_dialMVAmp,        m_sbMVAmp,        &FrameMBParams::mvAmplify,    "MV Amplify");
    connectKnobs(m_dialBlockFlatten, m_sbBlockFlatten, &FrameMBParams::blockFlatten, "Block Flatten");
    connectKnobs(m_dialRefScatter,   m_sbRefScatter,   &FrameMBParams::refScatter,   "Ref Scatter");
    connectKnobs(m_dialColorTwistU,  m_sbColorTwistU,  &FrameMBParams::colorTwistU,  "Color Twist U");
    connectKnobs(m_dialColorTwistV,  m_sbColorTwistV,  &FrameMBParams::colorTwistV,  "Color Twist V");
    // New pixel-domain knobs
    connectKnobs(m_dialPosterize,    m_sbPosterize,    &FrameMBParams::posterize,    "Posterize");
    connectKnobs(m_dialPixelShuffle, m_sbPixelShuffle, &FrameMBParams::pixelShuffle, "Pixel Shuffle");
    connectKnobs(m_dialSharpen,      m_sbSharpen,      &FrameMBParams::sharpen,      "Sharpen");
    connectKnobs(m_dialTempDiffAmp,  m_sbTempDiffAmp,  &FrameMBParams::tempDiffAmp,  "Temp Diff Amp");
    connectKnobs(m_dialHueRotate,    m_sbHueRotate,    &FrameMBParams::hueRotate,    "Hue Rotate");
    // Bitstream-domain knobs
    connectKnobs(m_dialBsMvdX,       m_sbBsMvdX,       &FrameMBParams::bsMvdX,       "BS MVD X");
    connectKnobs(m_dialBsMvdY,       m_sbBsMvdY,       &FrameMBParams::bsMvdY,       "BS MVD Y");
    connectKnobs(m_dialBsForceSkip,  m_sbBsForceSkip,  &FrameMBParams::bsForceSkip,  "BS Force Skip");
    connectKnobs(m_dialBsIntraMode,  m_sbBsIntraMode,  &FrameMBParams::bsIntraMode,  "BS Intra Mode");
    connectKnobs(m_dialBsMbType,     m_sbBsMbType,     &FrameMBParams::bsMbType,     "BS MB Type");
    connectKnobs(m_dialBsDctScale,   m_sbBsDctScale,   &FrameMBParams::bsDctScale,   "BS DCT Scale");
    connectKnobs(m_dialBsCbpZero,    m_sbBsCbpZero,    &FrameMBParams::bsCbpZero,    "BS CBP Zero");

    // ── Transient slider connections ──────────────────────────────────────────
    // Cascade controls are seed-specific envelope params — they must NOT
    // propagate across the active frame range.  Broadcasting cascadeLen to 300
    // frames turns every frame into its own seed, and the cascade guard
    // (activeEdits.contains) then blocks all synthetic descendant generation,
    // making the cascade a no-op.  Always write to m_currentFrame only.
    connect(m_sliderTransLen, &QSlider::valueChanged, this, [this](int v) {
        int oldVal = m_edits[m_currentFrame].cascadeLen;
        m_edits[m_currentFrame].cascadeLen = v;
        ControlLogger::instance().logKnobChange("Cascade Length", m_currentFrame, oldVal, v);
        m_lblTransLen->setText(QString::number(v) + " frames");
    });
    connect(m_sliderTransDecay, &QSlider::valueChanged, this, [this](int v) {
        int oldVal = m_edits[m_currentFrame].cascadeDecay;
        m_edits[m_currentFrame].cascadeDecay = v;
        ControlLogger::instance().logKnobChange("Cascade Decay", m_currentFrame, oldVal, v);
        m_lblTransDecay->setText(QString::number(v) + "%");
    });

    // ── Brush size + Clear controls ────────────────────────────────────────────
    // ── Preset row ────────────────────────────────────────────────────────────
    // Each preset populates all knobs with named datamosh algorithm values.
    // selectedMBs is left empty — empty = global mode in the renderer.
    // Uses a plain POD struct so aggregate initialisation works safely.
    struct PresetData {
        const char* name;
        // Field order matches FrameMBParams (excluding selectedMBs):
        int qpDelta, refDepth, ghostBlend, mvDriftX, mvDriftY, mvAmplify;
        int noiseLevel, pixelOffset, invertLuma;
        int chromaDriftX, chromaDriftY, chromaOffset;
        int spillRadius, sampleRadius;
        int cascadeLen, cascadeDecay;
        int blockFlatten, refScatter, colorTwistU, colorTwistV;
    };
    static const PresetData kPresets[] = {
        //                        qp  rd ghost  mvX  mvY amp  nz  px  inv  cxX  cxY  cOf  sp  sr  cl  cd  bf  rs  tu  tv
        { "Ghost Trail",           0,  1,  80,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0,  0, 40, 50,  0,  0,  0,  0 },
        { "MV Melt \xe2\x86\x92",  0,  1,   0,  60,   0,  3,   0,  0,   0,   0,   0,   0,  2,  0, 20, 30,  0,  0,  0,  0 },
        { "Chroma Ghost",          0,  1,  40,   0,   0,  1,   0,  0,   0,  35, -20,  15,  0,  0, 25, 40,  0,  0,  0,  0 },
        { "Pixel Melt",           51,  0,   0,   0,   0,  1, 180, 40,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0 },
        { "Full Freeze",          51,  1, 100,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0 },
        { "Scatter Wave",          0,  1,  30,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0, 20, 30, 60,  0, 30,  0,  0 },
        { "UV Colour Twist",       0,  1,  30,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0,  0, 20, 45,  0,  0, 40, 40 },
        { "Vortex",               20,  1,  50,  30, -30,  4,  60, 20,   0,   0,   0,   0,  3,  0, 35, 55,  0,  0,  0,  0 },
    };

    static constexpr int kMBBuiltinCount = (int)(sizeof(kPresets)/sizeof(kPresets[0]));

    auto* presetLabel = new QLabel("Preset:", this);
    presetLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
    m_presetCombo = new QComboBox(this);
    for (const auto& pr : kPresets) m_presetCombo->addItem(pr.name);
    m_presetCombo->setStyleSheet(
        "QComboBox { background:#1a1a1a; color:#ff88ff; border:1px solid #663366; "
        "font:8pt 'Consolas'; padding:1px 4px; }"
        "QComboBox::drop-down { border:none; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#ff88ff; "
        "selection-background-color:#441144; font:8pt 'Consolas'; }");

    auto* presetRow = new QHBoxLayout;
    presetRow->setSpacing(6);
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(m_presetCombo, 1);
    m_controlsLayout->addLayout(presetRow);

    // Preset management buttons
    {
        m_btnUserPresetSave   = new QPushButton("Save", this);
        m_btnUserPresetDel    = new QPushButton("Del",  this);
        m_btnUserPresetImport = new QPushButton("Import", this);

        const QString btnSS =
            "QPushButton { background:#1a1a1a; color:#aaa; border:1px solid #444; "
            "font:7pt 'Consolas'; padding:2px 5px; border-radius:2px; }"
            "QPushButton:hover { background:#222; color:#fff; border-color:#666; }"
            "QPushButton:disabled { color:#333; border-color:#222; }";
        m_btnUserPresetSave  ->setStyleSheet(btnSS);
        m_btnUserPresetDel   ->setStyleSheet(btnSS);
        m_btnUserPresetImport->setStyleSheet(btnSS);

        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(4);
        btnRow->addWidget(m_btnUserPresetSave);
        btnRow->addWidget(m_btnUserPresetDel);
        btnRow->addWidget(m_btnUserPresetImport);
        btnRow->addStretch();
        m_controlsLayout->addLayout(btnRow);

        refreshUserPresets();

        connect(m_btnUserPresetSave,   &QPushButton::clicked, this, &MacroblockWidget::onUserPresetSave);
        connect(m_btnUserPresetDel,    &QPushButton::clicked, this, &MacroblockWidget::onUserPresetDelete);
        connect(m_btnUserPresetImport, &QPushButton::clicked, this, &MacroblockWidget::onUserPresetImport);
    }

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx < 0) return;

        if (idx < kMBBuiltinCount) {
            // Built-in preset — convert PresetData to FrameMBParams
            static const PresetData kP[] = {
                {nullptr,  0,  1,  80,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0,  0, 40, 50,  0,  0,  0,  0 },
                {nullptr,  0,  1,   0,  60,   0,  3,   0,  0,   0,   0,   0,   0,  2,  0, 20, 30,  0,  0,  0,  0 },
                {nullptr,  0,  1,  40,   0,   0,  1,   0,  0,   0,  35, -20,  15,  0,  0, 25, 40,  0,  0,  0,  0 },
                {nullptr, 51,  0,   0,   0,   0,  1, 180, 40,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0 },
                {nullptr, 51,  1, 100,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0 },
                {nullptr,  0,  1,  30,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0, 20, 30, 60,  0, 30,  0,  0 },
                {nullptr,  0,  1,  30,   0,   0,  1,   0,  0,   0,   0,   0,   0,  0,  0, 20, 45,  0,  0, 40, 40 },
                {nullptr, 20,  1,  50,  30, -30,  4,  60, 20,   0,   0,   0,   0,  3,  0, 35, 55,  0,  0,  0,  0 },
            };
            const PresetData& pr = kP[idx];
            FrameMBParams p;
            p.qpDelta      = pr.qpDelta;
            p.refDepth     = pr.refDepth;
            p.ghostBlend   = pr.ghostBlend;
            p.mvDriftX     = pr.mvDriftX;
            p.mvDriftY     = pr.mvDriftY;
            p.mvAmplify    = pr.mvAmplify;
            p.noiseLevel   = pr.noiseLevel;
            p.pixelOffset  = pr.pixelOffset;
            p.invertLuma   = pr.invertLuma;
            p.chromaDriftX = pr.chromaDriftX;
            p.chromaDriftY = pr.chromaDriftY;
            p.chromaOffset = pr.chromaOffset;
            p.spillRadius  = pr.spillRadius;
            p.sampleRadius = pr.sampleRadius;
            p.cascadeLen   = pr.cascadeLen;
            p.cascadeDecay = pr.cascadeDecay;
            p.blockFlatten = pr.blockFlatten;
            p.refScatter   = pr.refScatter;
            p.colorTwistU  = pr.colorTwistU;
            p.colorTwistV  = pr.colorTwistV;
            applyControlParams(p);
        } else if (idx > kMBBuiltinCount) {
            // User preset (kMBBuiltinCount is the separator)
            const QString name = m_presetCombo->itemText(idx);
            if (name.isEmpty()) return;
            FrameMBParams p;
            if (PresetManager::loadMBEditor(name, p))
                applyControlParams(p);
        }

        if (m_btnUserPresetDel)
            m_btnUserPresetDel->setEnabled(idx > kMBBuiltinCount);
    });

    // ── Brush / clear row (wrapped in QWidget for pop-out reparenting) ────────
    m_brushBar = new QWidget(this);
    m_brushBar->setStyleSheet("background:transparent;");
    auto* ctrlRow = new QHBoxLayout(m_brushBar);
    ctrlRow->setContentsMargins(0, 0, 0, 0);
    ctrlRow->setSpacing(6);

    auto* brushLabel = new QLabel("Brush:", m_brushBar);
    brushLabel->setStyleSheet("color:#888; font:7pt 'Consolas';");
    brushLabel->setFixedWidth(38);
    ctrlRow->addWidget(brushLabel);

    m_sliderBrush = new QSlider(Qt::Horizontal, this);
    m_sliderBrush->setRange(1, 16);
    m_sliderBrush->setValue(1);
    m_sliderBrush->setEnabled(false);
    m_sliderBrush->setFixedWidth(90);
    m_sliderBrush->setToolTip("Brush size: number of MBs painted per stroke");
    m_sliderBrush->setStyleSheet(
        "QSlider::groove:horizontal { background:#1e1e1e; height:5px; border-radius:2px; }"
        "QSlider::handle:horizontal { background:#888; width:12px; height:12px; "
        "  margin:-4px 0; border-radius:6px; }"
        "QSlider::sub-page:horizontal { background:#555; border-radius:2px; }"
        "QSlider:disabled::handle:horizontal { background:#333; }"
        "QSlider:disabled::sub-page:horizontal { background:#1e1e1e; }");
    ctrlRow->addWidget(m_sliderBrush);

    m_lblBrush = new QLabel("1", this);
    m_lblBrush->setStyleSheet("color:#888; font:7pt 'Consolas'; background:transparent;");
    m_lblBrush->setFixedWidth(18);
    m_lblBrush->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ctrlRow->addWidget(m_lblBrush);

    // +/- brush mode toggle (select vs deselect)
    m_btnBrushAdd = new QPushButton("+", this);
    m_btnBrushSub = new QPushButton("\xe2\x80\x93", this);  // en-dash for minus
    m_btnBrushAdd->setFixedSize(22, 22);
    m_btnBrushSub->setFixedSize(22, 22);
    m_btnBrushAdd->setToolTip("Select mode (paint MBs)");
    m_btnBrushSub->setToolTip("Deselect mode (erase MBs)\nHold Alt to temporarily toggle");
    m_btnBrushAdd->setCheckable(true);
    m_btnBrushSub->setCheckable(true);
    m_btnBrushAdd->setChecked(true);   // default: select mode
    m_btnBrushSub->setChecked(false);
    const QString toggleOnSS =
        "QPushButton { background:#1a2a1a; color:#00ff88; border:1px solid #00ff88; "
        "font:bold 9pt 'Consolas'; border-radius:3px; }"
        "QPushButton:hover { background:#223322; }";
    const QString toggleOffSS =
        "QPushButton { background:#1a1a1a; color:#555; border:1px solid #333; "
        "font:bold 9pt 'Consolas'; border-radius:3px; }"
        "QPushButton:hover { background:#222; color:#aaa; }";
    m_btnBrushAdd->setStyleSheet(toggleOnSS);
    m_btnBrushSub->setStyleSheet(toggleOffSS);
    ctrlRow->addWidget(m_btnBrushAdd);
    ctrlRow->addWidget(m_btnBrushSub);

    connect(m_btnBrushAdd, &QPushButton::clicked, this, [this, toggleOnSS, toggleOffSS]() {
        m_canvas->setDeselectMode(false);
        m_btnBrushAdd->setChecked(true);
        m_btnBrushSub->setChecked(false);
        m_btnBrushAdd->setStyleSheet(toggleOnSS);
        m_btnBrushSub->setStyleSheet(toggleOffSS);
    });
    connect(m_btnBrushSub, &QPushButton::clicked, this, [this, toggleOnSS, toggleOffSS]() {
        m_canvas->setDeselectMode(true);
        m_btnBrushAdd->setChecked(false);
        m_btnBrushSub->setChecked(true);
        m_btnBrushSub->setStyleSheet(toggleOnSS);
        m_btnBrushAdd->setStyleSheet(toggleOffSS);
    });

    ctrlRow->addStretch(1);

    m_btnClearFrame = new QPushButton("Clear Frame", this);
    m_btnClearAll   = new QPushButton("Clear All",   this);
    m_btnClearFrame->setStyleSheet(kDarkBtn);
    m_btnClearAll  ->setStyleSheet(kDarkBtn);
    m_btnClearFrame->setEnabled(false);
    m_btnClearAll  ->setEnabled(false);

    ctrlRow->addWidget(m_btnClearFrame);
    ctrlRow->addWidget(m_btnClearAll);
    m_controlsLayout->addWidget(m_brushBar);

    connect(m_sliderBrush, &QSlider::valueChanged, m_canvas, &MBCanvas::setBrushSize);
    connect(m_sliderBrush, &QSlider::valueChanged, this, [this](int v) {
        m_lblBrush->setText(QString::number(v));
    });
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
    ControlLogger::instance().beginSession(videoPath);
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

    // Cascade length max = number of frames that can follow the seed frame.
    m_sliderTransLen->setMaximum(qMax(1, m_totalFrames - 1));

    // Reset zoom to fit-to-canvas on new video load.
    m_zoom = 1.0f;
    { QSignalBlocker zb(m_sliderZoom);
      m_sliderZoom->setValue(100); }
    m_lblZoom->setText("1.0\u00d7");
    { QSize vpSize = m_canvasScroll->viewport()->size();
      if (!vpSize.isEmpty()) m_canvas->resize(vpSize); }

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

    // Keep cascade length max in sync with (potentially reduced) frame count.
    m_sliderTransLen->setMaximum(qMax(1, m_totalFrames - 1));
    // Clamp any existing cascadeLen values that now exceed the new max.
    int newMax = m_sliderTransLen->maximum();
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it)
        it->cascadeLen = qMin(it->cascadeLen, newMax);

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
    m_lblTransDecay->setText(QString::number(p.cascadeDecay) + "%");
    m_sliderTransLen->blockSignals(false);
    m_sliderTransDecay->blockSignals(false);
    setKnob(m_dialBlockFlatten, m_sbBlockFlatten, p.blockFlatten);
    setKnob(m_dialRefScatter,   m_sbRefScatter,   p.refScatter);
    setKnob(m_dialColorTwistU,  m_sbColorTwistU,  p.colorTwistU);
    setKnob(m_dialColorTwistV,  m_sbColorTwistV,  p.colorTwistV);
    // New pixel-domain
    setKnob(m_dialPosterize,    m_sbPosterize,    p.posterize);
    setKnob(m_dialPixelShuffle, m_sbPixelShuffle, p.pixelShuffle);
    setKnob(m_dialSharpen,      m_sbSharpen,      p.sharpen);
    setKnob(m_dialTempDiffAmp,  m_sbTempDiffAmp,  p.tempDiffAmp);
    setKnob(m_dialHueRotate,    m_sbHueRotate,    p.hueRotate);
    // Bitstream-domain
    setKnob(m_dialBsMvdX,       m_sbBsMvdX,       p.bsMvdX);
    setKnob(m_dialBsMvdY,       m_sbBsMvdY,       p.bsMvdY);
    setKnob(m_dialBsForceSkip,  m_sbBsForceSkip,  p.bsForceSkip);
    setKnob(m_dialBsIntraMode,  m_sbBsIntraMode,  p.bsIntraMode);
    setKnob(m_dialBsMbType,     m_sbBsMbType,     p.bsMbType);
    setKnob(m_dialBsDctScale,   m_sbBsDctScale,   p.bsDctScale);
    setKnob(m_dialBsCbpZero,    m_sbBsCbpZero,    p.bsCbpZero);

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
    int oldCount = m_edits.value(m_currentFrame).selectedMBs.size();
    m_edits[m_currentFrame].selectedMBs = sel;
    ControlLogger::instance().logMBSelectionChange(m_currentFrame, oldCount, sel.size());
    emit mbSelectionChanged(sel);
}

void MacroblockWidget::setActiveFrameRange(const QVector<int>& frames)
{
    m_activeRange = frames;
}

void MacroblockWidget::loadEditMap(const MBEditMap& edits)
{
    m_edits = edits;
    loadKnobsFromCurrentFrame();
    m_canvas->loadSelection(m_edits.value(m_currentFrame).selectedMBs);
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
    m_btnPrev           ->setEnabled(enabled);
    m_btnNext           ->setEnabled(enabled);
    m_btnPopOutCanvas   ->setEnabled(enabled);
    m_sliderBrush       ->setEnabled(enabled);
    m_sliderZoom        ->setEnabled(enabled);
    m_btnClearFrame   ->setEnabled(enabled);
    m_btnClearAll     ->setEnabled(enabled);
    m_sliderTransLen  ->setEnabled(enabled);
    m_sliderTransDecay->setEnabled(enabled);
    for (QDial*    d : m_allDials)     d->setEnabled(enabled);
    for (QSpinBox* s : m_allSpinboxes) s->setEnabled(enabled);
}

// =============================================================================
// eventFilter — handles viewport resize (canvas size sync) + canvas wheel (zoom)
//               + canvas pop-out window close (re-dock).
// =============================================================================
bool MacroblockWidget::eventFilter(QObject* obj, QEvent* e)
{
    // ── Viewport resized: keep canvas at zoom × viewport size ────────────────
    if (obj == m_canvasScroll->viewport() && e->type() == QEvent::Resize) {
        QSize vpSize = m_canvasScroll->viewport()->size();
        m_canvas->resize(QSize(qRound(vpSize.width()  * m_zoom),
                               qRound(vpSize.height() * m_zoom)));
        return false; // let the event propagate normally
    }
    // ── Canvas wheel: zoom in/out ─────────────────────────────────────────────
    if (obj == m_canvas && e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        float factor = (we->angleDelta().y() > 0) ? 1.25f : (1.0f / 1.25f);
        float newZoom = qBound(1.0f, m_zoom * factor, 5.0f);
        if (!qFuzzyCompare(newZoom, m_zoom)) {
            m_zoom = newZoom;
            QSignalBlocker sb(m_sliderZoom);
            m_sliderZoom->setValue(qRound(m_zoom * 100.0f));
            m_lblZoom->setText(QString::number(m_zoom, 'f', 1) + "\u00d7");
            QSize vpSize = m_canvasScroll->viewport()->size();
            m_canvas->resize(QSize(qRound(vpSize.width()  * m_zoom),
                                   qRound(vpSize.height() * m_zoom)));
        }
        we->accept();
        return true; // consume — do not scroll the viewport
    }
    // ── Pop-out window closed: re-dock canvas into splitter ───────────────────
    if (obj == m_popOutWindow && e->type() == QEvent::Close) {
        m_controlsLayout->insertWidget(0, m_navBar);
        m_navBar->show();
        m_innerSplitter->insertWidget(0, m_canvasScroll);
        m_canvasScroll->show();
        m_controlsLayout->addWidget(m_brushBar);
        m_brushBar->show();
        m_popOutWindow->deleteLater();
        m_popOutWindow = nullptr;
        if (m_btnPopOutCanvas) {
            m_btnPopOutCanvas->setText("\u2922");
        }
        m_canvasIsPopped = false;
        return true; // suppress the close; widgets are now re-docked
    }
    return QWidget::eventFilter(obj, e);
}

// =============================================================================
// User preset API
// =============================================================================

FrameMBParams MacroblockWidget::currentControlParams() const
{
    return m_edits.value(m_currentFrame);
    // Note: the returned struct includes selectedMBs from the current frame.
    // Callers that want only knob values should ignore selectedMBs.
}

void MacroblockWidget::applyControlParams(const FrameMBParams& p)
{
    const QVector<int>& range = m_activeRange.isEmpty()
                                ? QVector<int>{m_currentFrame}
                                : m_activeRange;

    for (QDial*    d : m_allDials)     d->blockSignals(true);
    for (QSpinBox* s : m_allSpinboxes) s->blockSignals(true);

    for (int fi : range) {
        FrameMBParams& ep = m_edits[fi];
        // Preserve the selection on each frame; apply only the control values.
        ep.qpDelta      = p.qpDelta;
        ep.mvDriftX     = p.mvDriftX;
        ep.mvDriftY     = p.mvDriftY;
        ep.refDepth     = p.refDepth;
        ep.ghostBlend   = p.ghostBlend;
        ep.noiseLevel   = p.noiseLevel;
        ep.pixelOffset  = p.pixelOffset;
        ep.invertLuma   = p.invertLuma;
        ep.chromaDriftX = p.chromaDriftX;
        ep.chromaDriftY = p.chromaDriftY;
        ep.chromaOffset = p.chromaOffset;
        ep.spillRadius  = p.spillRadius;
        ep.sampleRadius = p.sampleRadius;
        ep.mvAmplify    = p.mvAmplify;
        ep.cascadeLen   = p.cascadeLen;
        ep.cascadeDecay = p.cascadeDecay;
        ep.blockFlatten = p.blockFlatten;
        ep.refScatter   = p.refScatter;
        ep.colorTwistU  = p.colorTwistU;
        ep.colorTwistV  = p.colorTwistV;
        // New pixel-domain
        ep.posterize    = p.posterize;
        ep.pixelShuffle = p.pixelShuffle;
        ep.sharpen      = p.sharpen;
        ep.tempDiffAmp  = p.tempDiffAmp;
        ep.hueRotate    = p.hueRotate;
        // Bitstream-domain
        ep.bsMvdX       = p.bsMvdX;
        ep.bsMvdY       = p.bsMvdY;
        ep.bsForceSkip  = p.bsForceSkip;
        ep.bsIntraMode  = p.bsIntraMode;
        ep.bsMbType     = p.bsMbType;
        ep.bsDctScale   = p.bsDctScale;
        ep.bsCbpZero    = p.bsCbpZero;
    }

    for (QDial*    d : m_allDials)     d->blockSignals(false);
    for (QSpinBox* s : m_allSpinboxes) s->blockSignals(false);

    loadKnobsFromCurrentFrame();
}

static constexpr int kMBBuiltinCountStatic = 8; // must match kMBBuiltinCount in ctor

void MacroblockWidget::refreshUserPresets()
{
    if (!m_presetCombo) return;
    QSignalBlocker sb(m_presetCombo);

    // Remove everything after built-in items (separator + old user items)
    while (m_presetCombo->count() > kMBBuiltinCountStatic)
        m_presetCombo->removeItem(m_presetCombo->count() - 1);

    const QStringList names = PresetManager::list(PresetManager::Type::MBEditor);
    if (!names.isEmpty()) {
        m_presetCombo->insertSeparator(kMBBuiltinCountStatic);
        for (const QString& n : names)
            m_presetCombo->addItem(n);
    }

    if (m_btnUserPresetDel)
        m_btnUserPresetDel->setEnabled(!names.isEmpty() &&
            m_presetCombo->currentIndex() > kMBBuiltinCountStatic);
}

// ── Preset slots ──────────────────────────────────────────────────────────────

void MacroblockWidget::onUserPresetSave()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        "Save MB Editor Preset",
        "Preset name:",
        QLineEdit::Normal,
        QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    FrameMBParams p = currentControlParams();
    if (!PresetManager::saveMBEditor(name, p)) {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not save preset \"%1\".").arg(name));
        return;
    }
    refreshUserPresets();
    const int idx = m_presetCombo->findText(PresetManager::sanitize(name));
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

void MacroblockWidget::onUserPresetDelete()
{
    const int ci = m_presetCombo->currentIndex();
    if (ci <= kMBBuiltinCountStatic) return; // can't delete built-in or separator

    const QString name = m_presetCombo->currentText();
    if (name.isEmpty()) return;

    const auto btn = QMessageBox::question(this, "Delete Preset",
        QString("Delete preset \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    PresetManager::deletePreset(PresetManager::Type::MBEditor, name);
    refreshUserPresets();
}

void MacroblockWidget::onUserPresetImport()
{
    const QString src = QFileDialog::getOpenFileName(this,
        "Import MB Editor Preset", QString(),
        "JSON Preset Files (*.json);;All Files (*)");
    if (src.isEmpty()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this,
        "Import Preset",
        "Name for imported preset:",
        QLineEdit::Normal,
        QFileInfo(src).completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (!PresetManager::importFile(PresetManager::Type::MBEditor, src, name)) {
        QMessageBox::warning(this, "Import Failed",
            "The selected file does not appear to be an MB editor preset.");
        return;
    }
    refreshUserPresets();
    const int idx = m_presetCombo->findText(PresetManager::sanitize(name));
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

#include "MacroblockWidget.moc"
