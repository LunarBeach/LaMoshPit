#pragma once

#include <QWidget>

class QProgressBar;
class QLabel;

// =============================================================================
// ProgressPanel
//
// A small dockable panel that shows the application's main operation progress
// bar plus a status label. Previously this lived as a banner inside
// QuickMoshWidget. It's now a standalone widget so the user can dock, float,
// resize or hide it like any other panel.
//
// MainWindow is the sole driver of this panel — it calls setProgressVisible()
// when a long-running operation starts, setRange()/setValue() as the worker
// reports progress, and setProgressVisible(false) when it finishes.
// =============================================================================
class ProgressPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgressPanel(QWidget* parent = nullptr);

    // Range + value passthroughs for the embedded QProgressBar.
    void setRange(int minimum, int maximum);
    void setValue(int value);

    // Show/hide the progress bar + status label. Disabled by default; turned
    // on when an operation begins. Panel widget itself stays visible so the
    // user knows where progress will appear.
    void setProgressVisible(bool visible);

    // Change the "Operation in progress..." status text (e.g. "Encoding...").
    void setStatusText(const QString& text);

private:
    QProgressBar* m_bar       { nullptr };
    QLabel*       m_statusLbl { nullptr };
};
