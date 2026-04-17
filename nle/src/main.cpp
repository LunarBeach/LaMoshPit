// =============================================================================
// LaMoshPit_NLE.exe — entry point.
//
// Phase 1 Step 2 stub: brings up an empty QMainWindow with MLT initialised
// in the background.  Proves that the build system produces a runnable
// Qt6 + MLT executable before we start forking Shotcut's timeline / player
// code in Step 5.
//
// Expected behaviour: a 1280x800 grey window titled "LaMoshPit NLE" opens,
// stays open, and exits cleanly on window-close.  Console reports MLT
// version on startup.
// =============================================================================

#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <QDebug>

#include <Mlt.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("LaMoshPit NLE");
    QCoreApplication::setOrganizationName("LaMoshPit");

    // MLT must init before any producer / consumer is touched.  The returned
    // repository is the set of discovered module DLLs; nullptr means MLT
    // couldn't find its modules (check MLT_REPOSITORY env var).
    Mlt::Repository* repo = Mlt::Factory::init();
    if (!repo) {
        qWarning() << "Mlt::Factory::init() returned null — modules not found.";
        return 2;
    }
    qInfo() << "MLT initialised, version:" << mlt_version_get_string();

    QMainWindow win;
    win.setWindowTitle("LaMoshPit NLE");
    win.resize(1280, 800);
    win.statusBar()->showMessage(
        QString("Ready — MLT %1").arg(mlt_version_get_string()));
    win.show();

    const int rc = app.exec();
    Mlt::Factory::close();
    return rc;
}
