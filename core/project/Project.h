#pragma once

// =============================================================================
// Project — a folder on disk containing a LaMoshPit working set.
//
// A project owns:
//   {folder}/project.json          ← manifest (metadata)
//   {folder}/imported_videos/      ← source imports + vNN render iterations
//   {folder}/thumbnails/           ← 160x90 PNG previews keyed by video filename
//   {folder}/logs/                 ← ControlLogger output (if project-routed)
//
// All user work lives inside the folder — nothing escapes to %APPDATA% or CWD.
// This makes projects trivially portable: copy the folder, send it to another
// user, they can open it from their install and everything works.  There is
// no "export" operation; the folder IS the export format.  Backups, sharing,
// and sync are the user's responsibility at the OS level.
//
// Lifetime: MainWindow holds exactly one Project* at a time.  Switching
// projects (File → Open / File → New) tears down the current one, constructs
// a fresh Project, then re-points all the subsystems (MediaBinWidget, import
// path, etc.) at the new paths.
// =============================================================================

#include <QString>
#include <QDateTime>
#include <memory>

class Project {
public:
    // Create a fresh project at the given folder path.  Creates the folder
    // (if absent) plus the imported_videos/, thumbnails/, logs/ subdirs, and
    // writes a minimal project.json.  Returns nullptr on failure (folder not
    // writeable, already contains a conflicting file, etc.); in that case
    // errorMsg is populated.
    static std::unique_ptr<Project> create(const QString& folderPath,
                                           const QString& name,
                                           QString& errorMsg);

    // Open an existing project at the given folder.  Requires project.json
    // to be present and parseable.  Missing subdirectories (imported_videos/
    // etc.) are silently re-created — users who hand-edit projects shouldn't
    // be penalised for accidentally deleting an empty thumbnails folder.
    static std::unique_ptr<Project> open(const QString& folderPath,
                                         QString& errorMsg);

    // Persist the manifest to disk.  Called automatically on close and when
    // the active video changes; exposed as a public method for explicit
    // File → Save Project user action.
    bool save(QString& errorMsg);

    // ── Paths ─────────────────────────────────────────────────────────────
    QString folderPath() const        { return m_folderPath; }
    QString manifestPath() const;                    // {folder}/project.json
    QString importedVideosDir() const;               // {folder}/imported_videos
    QString thumbnailsDir() const;                   // {folder}/thumbnails
    QString logsDir() const;                         // {folder}/logs

    // Thumbnail path for a given imported_videos/ file.  The video need not
    // exist yet — this is a pure string operation so callers can decide
    // whether to generate the thumbnail.  Maps "foo_imported.v03.mp4" →
    // "{folder}/thumbnails/foo_imported.v03.mp4.png".
    QString thumbnailPathFor(const QString& videoPath) const;

    // ── Metadata ──────────────────────────────────────────────────────────
    QString   name() const        { return m_name; }
    QDateTime created() const     { return m_created; }
    QDateTime lastOpened() const  { return m_lastOpened; }

    // Remember which video the user had loaded when they last closed, so the
    // app can restore that state on next open.  Setting this does NOT auto-
    // save — call save() when you want it to stick.
    QString activeVideo() const   { return m_activeVideo; }
    void setActiveVideo(const QString& absPath);

    // Rename the project.  Only updates the manifest; the folder on disk
    // keeps its original name (renaming the folder is the user's business).
    void setName(const QString& newName);

private:
    Project() = default;   // always constructed via create()/open()

    // Create any missing subdirs (imported_videos, thumbnails, logs).
    // Called by both create() and open() to tolerate partial projects.
    void ensureSubdirs();

    QString   m_folderPath;   // absolute path to the project folder
    QString   m_name;
    QDateTime m_created;
    QDateTime m_lastOpened;
    QString   m_activeVideo;  // absolute path, or empty
};
