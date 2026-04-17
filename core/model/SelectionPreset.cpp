#include "SelectionPreset.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
constexpr int kVersion = 1;

const char* kKeyVersion   = "version";
const char* kKeyName      = "name";
const char* kKeyCreated   = "created";
const char* kKeyMbCols    = "mbCols";
const char* kKeyMbRows    = "mbRows";
const char* kKeyFrameCount= "frameCount";
const char* kKeyFrames    = "frames";
const char* kKeyOffset    = "offset";
const char* kKeyMBs       = "mbs";

const char* kPresetSubdir = "selection_presets";

QString sanitizeName(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar& c : name) {
        if (c.isLetterOrNumber() || c == '-' || c == '_' || c == ' ')
            out.append(c);
        else
            out.append(QChar('_'));
    }
    out = out.trimmed();
    if (out.isEmpty()) out = "preset";
    return out;
}
}  // namespace

bool SelectionPresetIO::load(const QString& absPath,
                             SelectionPreset& out,
                             QString& errorMsg)
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) {
        errorMsg = "Cannot read preset: " + absPath;
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        errorMsg = "Preset is not a JSON object";
        return false;
    }
    const QJsonObject root = doc.object();

    out = SelectionPreset{};
    out.name       = root.value(kKeyName).toString();
    out.created    = QDateTime::fromString(root.value(kKeyCreated).toString(),
                                           Qt::ISODate);
    out.mbCols     = root.value(kKeyMbCols).toInt();
    out.mbRows     = root.value(kKeyMbRows).toInt();
    out.frameCount = root.value(kKeyFrameCount).toInt();

    const QJsonArray arr = root.value(kKeyFrames).toArray();
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const int offset = o.value(kKeyOffset).toInt(-1);
        if (offset < 0) continue;
        QSet<int> mbs;
        const QJsonArray mbArr = o.value(kKeyMBs).toArray();
        for (const QJsonValue& mv : mbArr) {
            const int idx = mv.toInt(-1);
            if (idx >= 0) mbs.insert(idx);
        }
        if (!mbs.isEmpty()) out.frames.insert(offset, mbs);
    }

    if (out.mbCols <= 0 || out.mbRows <= 0 || out.frameCount <= 0) {
        errorMsg = "Preset has invalid grid or frame count";
        return false;
    }
    if (out.name.isEmpty()) {
        out.name = QFileInfo(absPath).completeBaseName();
    }
    return true;
}

bool SelectionPresetIO::save(const QString& absPath,
                             const SelectionPreset& preset,
                             QString& errorMsg)
{
    QDir parent = QFileInfo(absPath).dir();
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        errorMsg = "Cannot create preset dir: " + parent.absolutePath();
        return false;
    }

    QJsonObject root;
    root[kKeyVersion]    = kVersion;
    root[kKeyName]       = preset.name;
    root[kKeyCreated]    = (preset.created.isValid()
                            ? preset.created
                            : QDateTime::currentDateTimeUtc())
                                .toString(Qt::ISODate);
    root[kKeyMbCols]     = preset.mbCols;
    root[kKeyMbRows]     = preset.mbRows;
    root[kKeyFrameCount] = preset.frameCount;

    QJsonArray arr;
    for (auto it = preset.frames.constBegin();
         it != preset.frames.constEnd(); ++it)
    {
        QJsonArray mbArr;
        // Sort for determinism (makes diffs + sharing friendlier).
        QList<int> sorted = it.value().values();
        std::sort(sorted.begin(), sorted.end());
        for (int v : sorted) mbArr.append(v);
        QJsonObject o;
        o[kKeyOffset] = it.key();
        o[kKeyMBs]    = mbArr;
        arr.append(o);
    }
    root[kKeyFrames] = arr;

    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMsg = "Cannot write preset: " + absPath;
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return true;
}

QString SelectionPresetIO::userPresetDir()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir  = base + "/" + kPresetSubdir;
    QDir().mkpath(dir);
    return dir;
}

QString SelectionPresetIO::userPresetPathFor(const QString& presetName)
{
    return userPresetDir() + "/" + sanitizeName(presetName) + ".json";
}

QStringList SelectionPresetIO::listUserPresets()
{
    QStringList out;
    QDir d(userPresetDir());
    if (!d.exists()) return out;
    const QStringList files = d.entryList(
        QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const QString& fn : files) {
        out.append(d.filePath(fn));
    }
    return out;
}
