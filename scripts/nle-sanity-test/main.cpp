// =============================================================================
// MSYS2/MinGW + Qt6 + MLT toolchain sanity test.
//
// Pops up a Qt window showing the MLT version string.  If this builds and
// runs, the toolchain we'll use for LaMoshPit_NLE.exe is correctly set up.
//
// Part of Phase 1 Step 1 of the NLE rebuild.  Delete this directory once
// the real nle/ source tree lands in Step 2.
// =============================================================================

#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <Mlt.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // MLT factory must be initialised once per process before any
    // Mlt::Producer/Consumer/Filter is created.  init() returns the
    // Mlt::Repository* (or nullptr if modules couldn't be located).
    Mlt::Repository* repo = Mlt::Factory::init();

    const QString mltVer  = QString::fromUtf8(mlt_version_get_string());
    const QString repoMsg = repo
        ? QStringLiteral("repo loaded")
        : QStringLiteral("repo NOT loaded — check MLT_REPOSITORY env var");

    QWidget win;
    win.setWindowTitle("LaMoshPit NLE — MSYS2 Toolchain Sanity Test");
    auto* layout = new QVBoxLayout(&win);
    layout->addWidget(new QLabel(QString("MLT version:  %1").arg(mltVer)));
    layout->addWidget(new QLabel(QString("Repository:   %1").arg(repoMsg)));
    layout->addWidget(new QLabel(
        "If you can read this, the MinGW Qt6 + MLT toolchain is working."));
    win.resize(500, 160);
    win.show();

    const int rc = app.exec();
    Mlt::Factory::close();
    return rc;
}
