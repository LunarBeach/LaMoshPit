#include "SelectionMap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
const char* kKeyMaps = "maps";
const char* kKeyName = "name";
const char* kKeyFile = "file";  // bare filename (relative to selection_maps/)
}  // namespace

QString SelectionMap::sidecarPath(const QString& clipVideoPath)
{
    return clipVideoPath + ".maps.json";
}

QList<SelectionMapEntry> SelectionMap::load(const QString& clipVideoPath,
                                            const QString& projectMapsDir)
{
    QList<SelectionMapEntry> result;
    const QString p = sidecarPath(clipVideoPath);
    QFile f(p);
    if (!f.exists()) return result;
    if (!f.open(QIODevice::ReadOnly)) return result;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return result;
    const QJsonArray arr = doc.object().value(kKeyMaps).toArray();

    const QDir mapsDir(projectMapsDir);
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        SelectionMapEntry e;
        e.name    = o.value(kKeyName).toString();
        const QString file = o.value(kKeyFile).toString();
        if (e.name.isEmpty() || file.isEmpty()) continue;
        e.absPath = QDir::cleanPath(mapsDir.filePath(file));
        // Skip entries whose file is missing on disk — don't surface
        // broken maps to the UI at all.
        if (!QFile::exists(e.absPath)) continue;
        result.append(e);
    }
    return result;
}

bool SelectionMap::save(const QString& clipVideoPath,
                        const QList<SelectionMapEntry>& entries,
                        QString& errorMsg)
{
    QJsonArray arr;
    for (const SelectionMapEntry& e : entries) {
        QJsonObject o;
        o[kKeyName] = e.name;
        o[kKeyFile] = QFileInfo(e.absPath).fileName();
        arr.append(o);
    }
    QJsonObject root;
    root[kKeyMaps] = arr;

    const QString p = sidecarPath(clipVideoPath);
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMsg = "Cannot write map sidecar: " + p;
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool SelectionMap::append(const QString& clipVideoPath,
                          const SelectionMapEntry& entry,
                          const QString& projectMapsDir,
                          QString& errorMsg)
{
    QList<SelectionMapEntry> existing = load(clipVideoPath, projectMapsDir);
    existing.append(entry);
    return save(clipVideoPath, existing, errorMsg);
}

QString SelectionMap::copyIntoProject(const QString& sourcePath,
                                      const QString& projectMapsDir,
                                      QString& errorMsg)
{
    QDir dir(projectMapsDir);
    if (!dir.exists()) {
        if (!QDir().mkpath(projectMapsDir)) {
            errorMsg = "Cannot create selection_maps dir: " + projectMapsDir;
            return QString();
        }
    }

    const QFileInfo srcFi(sourcePath);
    if (!srcFi.exists()) {
        errorMsg = "Source map video not found: " + sourcePath;
        return QString();
    }

    const QString baseName = srcFi.completeBaseName();
    const QString suffix   = srcFi.suffix().isEmpty() ? QString("mp4")
                                                      : srcFi.suffix();

    // De-duplicate on collision: name.mp4 → name_1.mp4 → name_2.mp4 → …
    QString candidate = baseName + "." + suffix;
    int n = 1;
    while (QFile::exists(dir.filePath(candidate))) {
        candidate = baseName + "_" + QString::number(n) + "." + suffix;
        ++n;
    }

    const QString destAbs = dir.filePath(candidate);
    if (!QFile::copy(sourcePath, destAbs)) {
        errorMsg = "Cannot copy map video to project: " + destAbs;
        return QString();
    }
    return destAbs;
}
