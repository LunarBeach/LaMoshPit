#include <QApplication>
#include <QIcon>
#include <QSplashScreen>
#include <QPixmap>
#include <QTimer>
#include <QFont>
#include "gui/MainWindow.h"

// =============================================================================
// Global application stylesheet — modern dark video-editor look.
// Darker backgrounds, white/neon-green text, green accent on interactive focus.
// Per-widget inline stylesheets in each widget file take precedence here.
// =============================================================================
static const char* kGlobalStyle = R"(

/* ── Base ─────────────────────────────────────────────────────────────────── */
QWidget {
    background: #0a0a0a;
    color: #ffffff;
    font-family: 'Consolas', 'Courier New', monospace;
    font-size: 9pt;
}

/* ── Menu bar ─────────────────────────────────────────────────────────────── */
QMenuBar {
    background: #080808;
    color: #cccccc;
    border-bottom: 1px solid #1c1c1c;
    spacing: 0;
}
QMenuBar::item {
    padding: 5px 12px;
    background: transparent;
}
QMenuBar::item:selected, QMenuBar::item:pressed {
    background: #181818;
    color: #00ff88;
}
QMenu {
    background: #111111;
    color: #cccccc;
    border: 1px solid #272727;
}
QMenu::item {
    padding: 5px 20px 5px 12px;
}
QMenu::item:selected {
    background: #1a1a1a;
    color: #00ff88;
}
QMenu::separator {
    height: 1px;
    background: #222222;
    margin: 3px 0;
}
QMenu::indicator { width: 14px; height: 14px; }
QMenu::indicator:checked {
    image: none;
    background: #00ff88;
    border-radius: 2px;
}

/* ── Status bar ───────────────────────────────────────────────────────────── */
QStatusBar {
    background: #060606;
    color: #555555;
    border-top: 1px solid #181818;
    font: 8pt 'Consolas';
}
QStatusBar::item { border: none; }

/* ── Splitters ────────────────────────────────────────────────────────────── */
QSplitter { background: #0a0a0a; }
QSplitter::handle {
    background: #161616;
}
QSplitter::handle:horizontal {
    width: 4px;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #0e0e0e, stop:0.5 #232323, stop:1 #0e0e0e);
}
QSplitter::handle:vertical {
    height: 4px;
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #0e0e0e, stop:0.5 #232323, stop:1 #0e0e0e);
}
QSplitter::handle:hover {
    background: #00ff88 !important;
}

/* ── Scroll bars ──────────────────────────────────────────────────────────── */
QScrollBar:vertical {
    background: #0e0e0e;
    width: 8px;
    border: none;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #2a2a2a;
    border-radius: 4px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover  { background: #00ff88; }
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical      { height: 0; }
QScrollBar:horizontal {
    background: #0e0e0e;
    height: 8px;
    border: none;
}
QScrollBar::handle:horizontal {
    background: #2a2a2a;
    border-radius: 4px;
    min-width: 24px;
}
QScrollBar::handle:horizontal:hover { background: #00ff88; }
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal     { width: 0; }

/* ── Group boxes ──────────────────────────────────────────────────────────── */
QGroupBox {
    background: #0e0e0e;
    border: 1px solid #222222;
    border-radius: 5px;
    margin-top: 10px;
    padding-top: 4px;
    font: bold 8pt 'Consolas';
    color: #00ff88;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 5px;
    color: #00ff88;
    background: #0a0a0a;
}

/* ── Tool tips ────────────────────────────────────────────────────────────── */
QToolTip {
    background: #151515;
    color: #cccccc;
    border: 1px solid #333333;
    font: 8pt 'Consolas';
    padding: 4px 6px;
    opacity: 230;
}

/* ── Scroll areas ─────────────────────────────────────────────────────────── */
QScrollArea { background: #0a0a0a; border: none; }

/* ── Progress bar ─────────────────────────────────────────────────────────── */
QProgressBar {
    background: #111111;
    border: 1px solid #252525;
    border-radius: 3px;
    text-align: center;
    color: #ffffff;
    font: 7pt 'Consolas';
}
QProgressBar::chunk {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #008844, stop:1 #00ff88);
    border-radius: 2px;
}

)";

// =============================================================================
// main
// =============================================================================

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application icon (shows in taskbar / alt-tab)
    QIcon appIcon(":/assets/ico/LA_ICO_5.ico");
    if (appIcon.isNull())
        appIcon = QIcon(":/assets/png/LA_ICO_5.png"); // PNG fallback
    app.setWindowIcon(appIcon);

    // Global dark theme
    app.setStyleSheet(kGlobalStyle);

    // Default application font
    QFont defaultFont("Consolas", 9);
    defaultFont.setStyleHint(QFont::Monospace);
    app.setFont(defaultFont);

    // ── Splash screen ────────────────────────────────────────────────────────
    QPixmap splashPx(":/assets/png/LA_ICO_5.png");
    QSplashScreen* splash = nullptr;

    if (!splashPx.isNull()) {
        // Scale to a reasonable splash size (max 480px wide, preserve aspect)
        if (splashPx.width() > 480)
            splashPx = splashPx.scaledToWidth(480, Qt::SmoothTransformation);

        splash = new QSplashScreen(splashPx, Qt::WindowStaysOnTopHint);
        splash->setStyleSheet("background: #080808;");
        splash->showMessage("  Loading LaMoshPit...",
                            Qt::AlignBottom | Qt::AlignLeft,
                            QColor(0x00, 0xff, 0x88));
        splash->show();
        app.processEvents();
    }

    // ── Main window ──────────────────────────────────────────────────────────
    MainWindow window;
    window.setWindowTitle("Lee Anne's Mosh Pit");
    window.setWindowIcon(appIcon);
    window.resize(1440, 900);
    window.show();

    // Close splash once main window is up
    if (splash) {
        splash->finish(&window);
        delete splash;
    }

    return app.exec();
}
