#include "Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {

// Schema version for forward-compat.  Bumped when breaking changes ship.
// Readers tolerate missing keys; this field is for future migration logic.
//   v1 : manifest metadata only (name, created, lastOpened, activeVideo)
//   v2 : adds "sequencer", "frameRouter", and "clipEdits" sections for
//        Scope B session persistence (NLE state + per-clip in-flight edits)
constexpr int kManifestVersion = 2;

const char* kKeyName        = "name";
const char* kKeyVersion     = "manifestVersion";
const char* kKeyCreated     = "created";
const char* kKeyLastOpened  = "lastOpened";
const char* kKeyActiveVideo = "activeVideo";
const char* kKeySequencer   = "sequencer";
const char* kKeyFrameRouter = "frameRouter";
const char* kKeyClipEdits   = "clipEdits";

// Subdir names — kept in one place so the rest of the class doesn't hardcode.
const char* kSubdirImports    = "MoshVideoFolder";
const char* kSubdirThumbnails = "thumbnails";
const char* kSubdirLogs       = "logs";
const char* kSubdirMaps       = "selection_maps";
const char* kManifestFile     = "project.json";

}  // namespace

// =============================================================================
// create / open
// =============================================================================

std::unique_ptr<Project> Project::create(const QString& folderPath,
                                         const QString& name,
                                         QString& errorMsg)
{
    QDir dir(folderPath);
    if (!dir.exists()) {
        if (!QDir().mkpath(folderPath)) {
            errorMsg = "Could not create project folder: " + folderPath;
            return nullptr;
        }
    } else if (QFile::exists(dir.filePath(kManifestFile))) {
        errorMsg = "A project already exists at: " + folderPath;
        return nullptr;
    }

    auto p = std::unique_ptr<Project>(new Project());
    p->m_folderPath = QDir(folderPath).absolutePath();
    p->m_name       = name.isEmpty() ? QFileInfo(folderPath).fileName() : name;
    p->m_created    = QDateTime::currentDateTimeUtc();
    p->m_lastOpened = p->m_created;
    p->m_activeVideo.clear();
    p->ensureSubdirs();

    if (!p->save(errorMsg)) return nullptr;
    return p;
}

std::unique_ptr<Project> Project::open(const QString& folderPath,
                                       QString& errorMsg)
{
    const QDir dir(folderPath);
    const QString manifestAbs = dir.filePath(kManifestFile);
    if (!QFile::exists(manifestAbs)) {
        errorMsg = "No project manifest at: " + manifestAbs;
        return nullptr;
    }

    QFile f(manifestAbs);
    if (!f.open(QIODevice::ReadOnly)) {
        errorMsg = "Cannot read manifest: " + manifestAbs;
        return nullptr;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        errorMsg = "Manifest is not a JSON object: " + manifestAbs;
        return nullptr;
    }
    const QJsonObject root = doc.object();

    auto p = std::unique_ptr<Project>(new Project());
    p->m_folderPath  = dir.absolutePath();
    p->m_name        = root.value(kKeyName).toString(QFileInfo(folderPath).fileName());
    p->m_created     = QDateTime::fromString(root.value(kKeyCreated).toString(), Qt::ISODate);
    p->m_lastOpened  = QDateTime::currentDateTimeUtc();
    p->m_activeVideo = root.value(kKeyActiveVideo).toString();

    // v2 sections — silently absent when reading a v1 project.
    p->m_sequencerStateJson   = root.value(kKeySequencer).toObject();
    p->m_frameRouterStateJson = root.value(kKeyFrameRouter).toObject();
    const QJsonObject editsObj = root.value(kKeyClipEdits).toObject();
    for (auto it = editsObj.begin(); it != editsObj.end(); ++it) {
        if (it.value().isObject())
            p->m_clipEdits.insert(it.key(), it.value().toObject());
    }

    p->ensureSubdirs();

    // Stamp lastOpened immediately so the manifest reflects the actual open
    // even if the user never changes anything before quitting.  Ignored if
    // save fails — stale timestamps aren't worth aborting open over.
    QString saveErr;
    p->save(saveErr);

    return p;
}

// =============================================================================
// save
// =============================================================================

bool Project::save(QString& errorMsg)
{
    QJsonObject root;
    root[kKeyVersion]     = kManifestVersion;
    root[kKeyName]        = m_name;
    root[kKeyCreated]     = m_created.toString(Qt::ISODate);
    root[kKeyLastOpened]  = m_lastOpened.toString(Qt::ISODate);
    root[kKeyActiveVideo] = m_activeVideo;

    if (!m_sequencerStateJson.isEmpty())
        root[kKeySequencer] = m_sequencerStateJson;
    if (!m_frameRouterStateJson.isEmpty())
        root[kKeyFrameRouter] = m_frameRouterStateJson;
    if (!m_clipEdits.isEmpty()) {
        QJsonObject editsObj;
        for (auto it = m_clipEdits.begin(); it != m_clipEdits.end(); ++it)
            editsObj.insert(it.key(), it.value());
        root[kKeyClipEdits] = editsObj;
    }

    QFile f(manifestPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMsg = "Cannot write manifest: " + manifestPath();
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    clearDirty();
    return true;
}

// =============================================================================
// dirty tracking
// =============================================================================

void Project::markDirty()
{
    if (m_dirty) return;
    m_dirty = true;
    emit dirtyChanged(true);
}

void Project::clearDirty()
{
    if (!m_dirty) return;
    m_dirty = false;
    emit dirtyChanged(false);
}

// =============================================================================
// per-clip edit state
// =============================================================================

void Project::setClipEditJson(const QString& tokenizedClipPath,
                              const QJsonObject& state)
{
    if (tokenizedClipPath.isEmpty()) return;
    const auto it = m_clipEdits.constFind(tokenizedClipPath);
    if (it != m_clipEdits.constEnd() && it.value() == state) return;
    m_clipEdits.insert(tokenizedClipPath, state);
    markDirty();
}

QJsonObject Project::clipEditJson(const QString& tokenizedClipPath) const
{
    return m_clipEdits.value(tokenizedClipPath);
}

bool Project::hasClipEdit(const QString& tokenizedClipPath) const
{
    return m_clipEdits.contains(tokenizedClipPath);
}

void Project::clearClipEdit(const QString& tokenizedClipPath)
{
    if (!m_clipEdits.contains(tokenizedClipPath)) return;
    m_clipEdits.remove(tokenizedClipPath);
    markDirty();
}

QList<QString> Project::clipEditKeys() const
{
    return m_clipEdits.keys();
}

// =============================================================================
// sequencer + router opaque state setters (readers are inline in header)
// =============================================================================

void Project::setSequencerStateJson(const QJsonObject& obj)
{
    if (m_sequencerStateJson == obj) return;
    m_sequencerStateJson = obj;
    markDirty();
}

void Project::setFrameRouterStateJson(const QJsonObject& obj)
{
    if (m_frameRouterStateJson == obj) return;
    m_frameRouterStateJson = obj;
    markDirty();
}

// =============================================================================
// paths
// =============================================================================

QString Project::manifestPath() const
{
    return QDir(m_folderPath).filePath(kManifestFile);
}

QString Project::moshVideoFolder() const
{
    // Check the app-wide override (written by SettingsDialog).  When set, it
    // takes precedence over the per-project default; all imports, scans, and
    // token resolutions use the override path.  The key is read directly via
    // QSettings rather than via SettingsDialog because Project lives in core/
    // and must not depend on gui/.  The key name must stay in sync with
    // SettingsDialog::kMoshFolderKey.
    QSettings s("LaMoshPit", "LaMoshPit");
    const QString override_ =
        s.value("paths/moshVideoFolderOverride", QString()).toString();
    if (!override_.isEmpty())
        return QDir::cleanPath(override_);
    return QDir(m_folderPath).filePath(kSubdirImports);
}

// =============================================================================
// Path token helpers
// =============================================================================
// Saved paths (e.g. sequencer clip sources) use "{MoshVideoFolder}/foo.mp4"
// instead of hard-coded absolute paths so projects survive the user moving
// their video vault (changing the override in Settings, swapping drive
// letters, etc.).  These helpers do the round-trip.

QString Project::expandTokens(const QString& storedPath) const
{
    const QString token = QStringLiteral("{MoshVideoFolder}");
    if (!storedPath.startsWith(token)) return storedPath;
    QString rest = storedPath.mid(token.length());
    while (rest.startsWith('/') || rest.startsWith('\\'))
        rest.remove(0, 1);
    return QDir(moshVideoFolder()).filePath(rest);
}

QString Project::compressToTokens(const QString& absPath) const
{
    const QString base = QDir::cleanPath(moshVideoFolder());
    const QString abs  = QDir::cleanPath(absPath);
    if (!abs.startsWith(base, Qt::CaseInsensitive)) return absPath;
    QString rel = abs.mid(base.length());
    while (rel.startsWith('/') || rel.startsWith('\\'))
        rel.remove(0, 1);
    return QStringLiteral("{MoshVideoFolder}/") + rel;
}

QString Project::thumbnailsDir() const
{
    return QDir(m_folderPath).filePath(kSubdirThumbnails);
}

QString Project::logsDir() const
{
    return QDir(m_folderPath).filePath(kSubdirLogs);
}

QString Project::selectionMapsDir() const
{
    return QDir(m_folderPath).filePath(kSubdirMaps);
}

QString Project::thumbnailPathFor(const QString& videoPath) const
{
    // Use the bare filename (not the full absolute path) so a thumbnail is
    // tied to the video's name within the project, not its disk location.
    // If the project folder moves, the thumbnail mapping still holds.
    const QString baseName = QFileInfo(videoPath).fileName();
    return QDir(thumbnailsDir()).filePath(baseName + ".png");
}

// =============================================================================
// metadata setters
// =============================================================================

void Project::setActiveVideo(const QString& absPath)
{
    if (m_activeVideo == absPath) return;
    m_activeVideo = absPath;
    markDirty();
}

void Project::setName(const QString& newName)
{
    if (m_name == newName) return;
    m_name = newName;
    markDirty();
}

// =============================================================================

void Project::ensureSubdirs()
{
    QDir base(m_folderPath);
    base.mkpath(kSubdirImports);
    base.mkpath(kSubdirThumbnails);
    base.mkpath(kSubdirLogs);
    base.mkpath(kSubdirMaps);
}
