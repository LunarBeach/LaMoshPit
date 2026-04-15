#include "Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// Schema version for forward-compat.  Bumped when breaking changes ship.
// Readers tolerate missing keys; this field is for future migration logic.
constexpr int kManifestVersion = 1;

const char* kKeyName        = "name";
const char* kKeyVersion     = "manifestVersion";
const char* kKeyCreated     = "created";
const char* kKeyLastOpened  = "lastOpened";
const char* kKeyActiveVideo = "activeVideo";

// Subdir names — kept in one place so the rest of the class doesn't hardcode.
const char* kSubdirImports    = "imported_videos";
const char* kSubdirThumbnails = "thumbnails";
const char* kSubdirLogs       = "logs";
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

    QFile f(manifestPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMsg = "Cannot write manifest: " + manifestPath();
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return true;
}

// =============================================================================
// paths
// =============================================================================

QString Project::manifestPath() const
{
    return QDir(m_folderPath).filePath(kManifestFile);
}

QString Project::importedVideosDir() const
{
    return QDir(m_folderPath).filePath(kSubdirImports);
}

QString Project::thumbnailsDir() const
{
    return QDir(m_folderPath).filePath(kSubdirThumbnails);
}

QString Project::logsDir() const
{
    return QDir(m_folderPath).filePath(kSubdirLogs);
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
    m_activeVideo = absPath;
}

void Project::setName(const QString& newName)
{
    m_name = newName;
}

// =============================================================================

void Project::ensureSubdirs()
{
    QDir base(m_folderPath);
    base.mkpath(kSubdirImports);
    base.mkpath(kSubdirThumbnails);
    base.mkpath(kSubdirLogs);
}
