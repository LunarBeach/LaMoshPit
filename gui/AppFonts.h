#pragma once

#include <QFont>
#include <QString>

// Role-based font access. Families are resolved from the .otf files bundled
// via resources.qrc on application startup. Call loadApplicationFonts() once
// from main() before building any widgets.
//
//   heading  — Ethnocentric-Regular.      Tab labels, window titles, group headings.
//   body     — CreatoDisplay-Regular.     Default UI text, labels, menus, buttons.
//   display  — Nodo-Regular.              Large display text (splash, dramatic headings).
namespace AppFonts {

void loadApplicationFonts();

QString headingFamily();
QString bodyFamily();
QString displayFamily();

QFont heading(int pointSize = 11, QFont::Weight weight = QFont::Normal);
QFont body(int pointSize = 10,    QFont::Weight weight = QFont::Normal);
QFont display(int pointSize = 24, QFont::Weight weight = QFont::Normal);

}  // namespace AppFonts
