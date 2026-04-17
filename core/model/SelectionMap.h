#pragma once

#include <QString>
#include <QList>

// =============================================================================
// SelectionMap
//
// Per-clip metadata describing black-and-white "map" videos that drive MB
// selection.  Each imported project clip may have zero or more maps.  Storage
// is a sidecar JSON file kept alongside the clip video:
//
//   {project}/MoshVideoFolder/myclip.mp4
//   {project}/MoshVideoFolder/myclip.mp4.maps.json
//
// Map video files themselves are copied into the project on import so that
// projects remain self-contained and portable:
//
//   {project}/selection_maps/<filename>.mp4
//
// The sidecar stores only the bare filename (relative to selection_maps/),
// not an absolute path.  Absolute paths are resolved at load time.
// =============================================================================
struct SelectionMapEntry {
    QString name;        // User-visible display name
    QString absPath;     // Absolute path to the map video on disk
};

class SelectionMap {
public:
    // Path to the sidecar file for a given clip video.  Pure string op —
    // does not check existence.  Format: "<videoPath>.maps.json".
    static QString sidecarPath(const QString& clipVideoPath);

    // Load all map entries associated with `clipVideoPath`, skipping any
    // whose map file is missing on disk.  `projectMapsDir` is the absolute
    // path of {project}/selection_maps/.  Returns empty list if the sidecar
    // is absent, unreadable, or every entry's file is missing.
    static QList<SelectionMapEntry> load(const QString& clipVideoPath,
                                         const QString& projectMapsDir);

    // Overwrite the sidecar with the given entries.  Entries' absPath must
    // already resolve against projectMapsDir (we store QFileInfo::fileName
    // for portability).  Returns true on success.
    static bool save(const QString& clipVideoPath,
                     const QList<SelectionMapEntry>& entries,
                     QString& errorMsg);

    // Append one entry to the clip's sidecar (load → push → save).
    static bool append(const QString& clipVideoPath,
                       const SelectionMapEntry& entry,
                       const QString& projectMapsDir,
                       QString& errorMsg);

    // Copy a source map video into {project}/selection_maps/, de-duplicating
    // the filename if a collision occurs (appends "_1", "_2", …).  Returns
    // the absolute path of the destination file on success, empty string on
    // failure (errorMsg populated).
    static QString copyIntoProject(const QString& sourcePath,
                                   const QString& projectMapsDir,
                                   QString& errorMsg);
};
