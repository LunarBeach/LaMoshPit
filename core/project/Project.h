#pragma once

// =============================================================================
// Project — a folder on disk containing a LaMoshPit working set.
//
// A project owns:
//   {folder}/project.json          ← manifest (metadata)
//   {folder}/MoshVideoFolder/      ← source imports + vNN render iterations
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

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <memory>

class Project : public QObject {
    Q_OBJECT
public:
    // Create a fresh project at the given folder path.  Creates the folder
    // (if absent) plus the MoshVideoFolder/, thumbnails/, logs/ subdirs, and
    // writes a minimal project.json.  Returns nullptr on failure (folder not
    // writeable, already contains a conflicting file, etc.); in that case
    // errorMsg is populated.
    static std::unique_ptr<Project> create(const QString& folderPath,
                                           const QString& name,
                                           QString& errorMsg);

    // Open an existing project at the given folder.  Requires project.json
    // to be present and parseable.  Missing subdirectories (MoshVideoFolder/
    // etc.) are silently re-created — users who hand-edit projects shouldn't
    // be penalised for accidentally deleting an empty thumbnails folder.
    static std::unique_ptr<Project> open(const QString& folderPath,
                                         QString& errorMsg);

    // Persist the manifest to disk.  Called automatically on close and when
    // the active video changes; exposed as a public method for explicit
    // File → Save Project user action.  On success, clears the dirty flag.
    bool save(QString& errorMsg);

    // ── Dirty tracking ────────────────────────────────────────────────────
    // The dirty flag tells the UI whether there are unsaved changes — shown
    // as a "*" in the window title and triggers a "save before close?" prompt
    // on app quit.  Subsystems that mutate project-saved state (MacroblockWidget
    // edits, GlobalParamsWidget changes, SequencerProject changes, clip-edit
    // captures on clip switch) call markDirty().  save() and successful load
    // clear it.
    bool isDirty() const { return m_dirty; }
    void markDirty();
    void clearDirty();

signals:
    // Emitted whenever the dirty state flips.  MainWindow listens to update
    // the window title's "*" indicator.
    void dirtyChanged(bool dirty);

public:

    // ── Paths ─────────────────────────────────────────────────────────────
    QString folderPath() const        { return m_folderPath; }
    QString manifestPath() const;                    // {folder}/project.json
    QString moshVideoFolder() const;                 // {folder}/MoshVideoFolder
                                                     //   (or override from Settings)

    // Round-trip helpers for the "{MoshVideoFolder}" path token.  Saved
    // resources (e.g. sequencer clip sources) use the token instead of an
    // absolute path so the project survives a drive-letter change or a user
    // moving the shared video vault to a new location.
    //
    //   expandTokens("{MoshVideoFolder}/foo.mp4")  -> "<resolved>/foo.mp4"
    //   compressToTokens("<resolved>/foo.mp4")     -> "{MoshVideoFolder}/foo.mp4"
    //
    // Paths not under moshVideoFolder() are passed through untouched by both
    // directions, so legacy absolute paths keep working.
    QString expandTokens(const QString& storedPath) const;
    QString compressToTokens(const QString& absPath) const;

    // ── Sequencer + router state (opaque JSON, owned by callers) ───────
    // SequencerProject / FrameRouter serialise themselves; Project just
    // holds the resulting QJsonObject and embeds it in project.json on
    // save.  Keeping it opaque here means Project stays decoupled from the
    // sequencer subsystem (no include-from-core/project/ into sequencer
    // types), and serialisation shape evolves without touching Project.
    void        setSequencerStateJson(const QJsonObject& obj);
    QJsonObject sequencerStateJson() const { return m_sequencerStateJson; }
    void        setFrameRouterStateJson(const QJsonObject& obj);
    QJsonObject frameRouterStateJson() const { return m_frameRouterStateJson; }

    // ── Per-clip edit state ─────────────────────────────────────────────
    // The project remembers, per imported clip, the MB-editor knob state +
    // painted selections + Global Encode Params the user had in flight
    // when they last had that clip loaded.  This makes the "switch clips,
    // work on another, come back, pick up where you left off" workflow
    // survive across clip swaps (and across save/close).
    //
    // Project stores the state as an opaque JSON blob keyed by tokenized
    // clip path ("{MoshVideoFolder}/foo.mp4").  MainWindow owns the widget-
    // to-JSON conversion (Project lives in core/ and has no widget types);
    // the JSON shape is documented at the capture call site.
    //
    // Tokenizing keys keeps the mapping robust against video-vault moves:
    // change the override in Settings and all stashed keys still resolve.
    void        setClipEditJson(const QString& tokenizedClipPath,
                                const QJsonObject& state);
    QJsonObject clipEditJson(const QString& tokenizedClipPath) const;
    bool        hasClipEdit(const QString& tokenizedClipPath) const;
    void        clearClipEdit(const QString& tokenizedClipPath);
    QList<QString> clipEditKeys() const;
    QString thumbnailsDir() const;                   // {folder}/thumbnails
    QString logsDir() const;                         // {folder}/logs
    QString selectionMapsDir() const;                // {folder}/selection_maps

    // Thumbnail path for a given MoshVideoFolder/ file.  The video need not
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

    // Create any missing subdirs (MoshVideoFolder, thumbnails, logs).
    // Called by both create() and open() to tolerate partial projects.
    void ensureSubdirs();

    QString   m_folderPath;   // absolute path to the project folder
    QString   m_name;
    QDateTime m_created;
    QDateTime m_lastOpened;
    QString   m_activeVideo;  // absolute path, or empty
    bool      m_dirty { false };

    // Keyed by tokenized clip path ("{MoshVideoFolder}/foo.mp4").  Values
    // are opaque to Project — MainWindow owns the shape.  Populated lazily
    // as the user switches away from clips; persisted in project.json.
    QHash<QString, QJsonObject> m_clipEdits;

    // Opaque JSON for the sequencer + router sub-states.  See the getter/
    // setter doc comments above for the rationale.
    QJsonObject m_sequencerStateJson;
    QJsonObject m_frameRouterStateJson;
};
