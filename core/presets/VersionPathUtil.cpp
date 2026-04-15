#include "VersionPathUtil.h"

#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

namespace {

// Match ".vNN" where NN is exactly two digits, immediately before ".mp4" at
// end of path.  Used to detect versioned filenames and extract the number.
// The 2-digit constraint means "v1", "v3", "v123" are NOT recognised as
// version markers — only v00..v99.  If we ever need more, widen the regex
// AND audit every caller that assumes 2-digit filenames sort correctly.
static const QRegularExpression kVersionRe(
    QStringLiteral(R"(\.v(\d{2})\.mp4$)"),
    QRegularExpression::CaseInsensitiveOption);

}  // namespace

namespace VersionPathUtil {

QString rootPath(const QString& anyVideoPath)
{
    QString out = anyVideoPath;
    const auto m = kVersionRe.match(out);
    if (m.hasMatch()) {
        // Replace the ".vNN.mp4" tail with ".mp4".
        out.replace(m.capturedStart(0), m.capturedLength(0),
                    QStringLiteral(".mp4"));
    }
    return out;
}

int versionOf(const QString& anyVideoPath)
{
    const auto m = kVersionRe.match(anyVideoPath);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QString nextVersionPath(const QString& anyVideoPath)
{
    const QString root = rootPath(anyVideoPath);
    const QFileInfo rootInfo(root);
    const QDir dir = rootInfo.dir();
    const QString rootBaseName = rootInfo.completeBaseName();   // "foo_imported"

    // Scan siblings for "foo_imported.vNN.mp4" and find the max NN.
    int maxVersion = 0;
    const QStringList entries = dir.entryList(
        QStringList{ rootBaseName + QStringLiteral(".v*.mp4") },
        QDir::Files, QDir::Name);
    for (const QString& name : entries) {
        const auto m = kVersionRe.match(name);
        if (!m.hasMatch()) continue;
        const int v = m.captured(1).toInt();
        if (v > maxVersion) maxVersion = v;
    }

    const int nextV = maxVersion + 1;
    const QString nextName = QString("%1.v%2.mp4")
        .arg(rootBaseName)
        .arg(nextV, 2, 10, QLatin1Char('0'));
    return dir.filePath(nextName);
}

QStringList listVersions(const QString& anyVideoPath)
{
    const QString root = rootPath(anyVideoPath);
    const QFileInfo rootInfo(root);
    const QDir dir = rootInfo.dir();
    const QString rootBaseName = rootInfo.completeBaseName();

    // Collect (version, absPath) pairs, then sort by version ascending.
    struct Entry { int v; QString path; };
    QList<Entry> entries;
    const QStringList names = dir.entryList(
        QStringList{ rootBaseName + QStringLiteral(".v*.mp4") },
        QDir::Files, QDir::Name);
    for (const QString& name : names) {
        const auto m = kVersionRe.match(name);
        if (!m.hasMatch()) continue;
        entries.append({ m.captured(1).toInt(), dir.filePath(name) });
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b){ return a.v < b.v; });

    QStringList result;
    result.reserve(entries.size());
    for (const Entry& e : entries) result << e.path;
    return result;
}

QString sidecarJsonPath(const QString& videoPath)
{
    return videoPath + QStringLiteral(".json");
}

QString previousVersionPath(const QString& currentVideoPath)
{
    const int curV = versionOf(currentVideoPath);
    if (curV <= 0) return QString();  // Can't go back before the import.

    // Walk downwards from curV-1 until we find an existing file, then fall
    // back to the root if nothing earlier was kept.
    const QStringList versions = listVersions(currentVideoPath);
    QString bestBelow;
    int bestBelowV = 0;
    for (const QString& p : versions) {
        const int v = versionOf(p);
        if (v < curV && v > bestBelowV) {
            bestBelowV = v;
            bestBelow = p;
        }
    }
    if (!bestBelow.isEmpty()) return bestBelow;

    // No older version exists — fall back to the root import if it's still
    // on disk (user may have deleted everything but v03, in which case we
    // can't usefully step back).
    const QString root = rootPath(currentVideoPath);
    if (QFileInfo::exists(root) && root != currentVideoPath) return root;
    return QString();
}

}  // namespace VersionPathUtil
