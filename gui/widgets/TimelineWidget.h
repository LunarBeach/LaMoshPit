#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVector>
#include <QPixmap>
#include <QColor>

// =============================================================================
// FrameThumbData — one slot in the strip
// =============================================================================
struct FrameThumbData {
    int   index       = 0;
    char  type        = '?';  // 'I' 'P' 'B' or '?'
    bool  isIDR       = false;
    bool  thumbLoaded = false;
    QPixmap thumb;
};

// =============================================================================
// FrameStripWidget — custom-painted inner widget inside the QScrollArea.
// Handles all drawing and mouse-based selection logic.
// =============================================================================
class FrameStripWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrameStripWidget(QWidget* parent = nullptr);

    void setFrameData(const QVector<char>& types, const QVector<bool>& idrs);
    void setThumbnail(int frameIndex, const QPixmap& thumb);
    void clear();

    const QVector<int>& selectedIndices() const { return m_selected; }
    void clearSelection();
    // Programmatically select a single frame (called from MainWindow on MB nav).
    void setSelection(int frameIdx);

    // Visual constants (used by TimelineWidget to size the scroll area)
    static constexpr int THUMB_W  = 96;
    static constexpr int THUMB_H  = 54;
    static constexpr int LABEL_H  = 16;
    static constexpr int BORDER   = 3;
    static constexpr int GAP      = 4;
    static constexpr int MARGIN_V = 4;
    // Derived
    static constexpr int CELL_W   = BORDER * 2 + THUMB_W;        // 102
    static constexpr int CELL_H   = BORDER * 2 + THUMB_H + LABEL_H; // 76
    static constexpr int SLOT_W   = CELL_W + GAP;                 // 106
    static constexpr int WIDGET_H = MARGIN_V * 2 + CELL_H;        // 84

signals:
    void selectionChanged(QVector<int> selected);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void drawCell(QPainter& p, int idx) const;
    QRect cellRect(int idx) const;
    int   cellAtX(int x)   const;
    QColor borderColor(char type) const;

    QVector<FrameThumbData> m_frames;
    QVector<int>            m_selected;
    int                     m_lastClicked = -1;
};

// =============================================================================
// TimelineWidget — public interface used by MainWindow.
// Owns the scroll area, strip widget, and thumbnail-loading thread.
// =============================================================================
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);
    ~TimelineWidget();

    // Load frame type/IDR data and kick off async thumbnail extraction.
    void loadVideo(const QString& videoPath,
                   const QVector<char>& frameTypes,
                   const QVector<bool>& idrFlags);

    // Legacy no-op kept for compatibility
    void setVideoSequence(class VideoSequence*) {}

    QVector<int> selectedFrames() const;
    void clearSelection();
    // Programmatically select a single frame and scroll it into view.
    void setSelection(int frameIdx);

signals:
    void frameSelected(int frameIndex);          // emitted on single-frame selection
    void selectionChanged(QVector<int> indices); // emitted on every selection change

private slots:
    void onThumbnailReady(int frameIndex, QPixmap thumb);
    void onLoadingDone(int totalCount);
    void onStripSelectionChanged(QVector<int> sel);

private:
    void stopLoader();

    QScrollArea*      m_scroll  = nullptr;
    FrameStripWidget* m_strip   = nullptr;

    QThread*  m_loaderThread  = nullptr;
    QObject*  m_loader        = nullptr;   // ThumbnailLoader, heap-owned by thread
};
