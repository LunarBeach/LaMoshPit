#include "AppFonts.h"

#include <QFontDatabase>
#include <QDebug>

namespace AppFonts {
namespace {

// Resolved family names, filled in by loadApplicationFonts(). If loading
// fails for any font, the corresponding family falls back to a system
// substitute so the app still renders.
QString g_heading = "Sans Serif";
QString g_body    = "Sans Serif";
QString g_display = "Sans Serif";

QString loadOne(const QString& resourcePath, const QString& fallback)
{
    const int id = QFontDatabase::addApplicationFont(resourcePath);
    if (id < 0) {
        qWarning() << "AppFonts: failed to load" << resourcePath;
        return fallback;
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qWarning() << "AppFonts: no families for" << resourcePath;
        return fallback;
    }
    return families.first();
}

}  // namespace

void loadApplicationFonts()
{
    // Body: multiple weights — loaded together so QFont::setWeight() works.
    loadOne(":/assets/fonts/creato_display/CreatoDisplay-Bold.otf",   "Sans Serif");
    loadOne(":/assets/fonts/creato_display/CreatoDisplay-Medium.otf", "Sans Serif");
    g_body    = loadOne(":/assets/fonts/creato_display/CreatoDisplay-Regular.otf", "Sans Serif");

    g_heading = loadOne(":/assets/fonts/ethnocentric/Ethnocentric-Regular.otf", g_body);
    g_display = loadOne(":/assets/fonts/Nodo.otf", g_heading);
}

QString headingFamily() { return g_heading; }
QString bodyFamily()    { return g_body; }
QString displayFamily() { return g_display; }

QFont heading(int pointSize, QFont::Weight weight)
{
    QFont f(g_heading, pointSize, weight);
    return f;
}
QFont body(int pointSize, QFont::Weight weight)
{
    QFont f(g_body, pointSize, weight);
    return f;
}
QFont display(int pointSize, QFont::Weight weight)
{
    QFont f(g_display, pointSize, weight);
    return f;
}

}  // namespace AppFonts
