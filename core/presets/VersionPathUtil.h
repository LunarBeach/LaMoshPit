#pragma once

#include <QString>
#include <QStringList>

// =============================================================================
// VersionPathUtil — helpers for the non-destructive render versioning scheme.
//
// LaMoshPit stores each render iteration as a new file alongside the original
// import, rather than overwriting in place.  The convention:
//
//   MoshVideoFolder/foo_imported.mp4          ← the root (original import)
//   MoshVideoFolder/foo_imported.v01.mp4      ← render 1
//   MoshVideoFolder/foo_imported.v02.mp4      ← render 2
//   MoshVideoFolder/foo_imported.v03.mp4      ← render 3 ...
//
// A sidecar JSON capturing the knob state that produced each iteration lives
// alongside the video: foo_imported.v01.mp4.json.
//
// These helpers are stateless string/filesystem operations.  They do NOT read
// or write any file content themselves — callers handle the I/O.  Keeping
// them in a single utility means the version-filename convention has exactly
// one definition; FrameTransformer, MediaBinWidget, and MainWindow all agree.
// =============================================================================
namespace VersionPathUtil {

    // Strip the ".vNN" suffix (if present) from a video path, returning the
    // root.  Idempotent — passing the root back in returns itself unchanged.
    //   "foo_imported.v03.mp4"  →  "foo_imported.mp4"
    //   "foo_imported.mp4"      →  "foo_imported.mp4"
    //   "foo.v3.mp4"            →  "foo.v3.mp4"   (must be 2 digits: v01..v99)
    QString rootPath(const QString& anyVideoPath);

    // Extract the version number embedded in the path, or 0 if the path is a
    // root (no version suffix).
    //   "foo_imported.v03.mp4"  →  3
    //   "foo_imported.mp4"      →  0
    int versionOf(const QString& anyVideoPath);

    // Given any video in a family (root or versioned), return the path that
    // the NEXT render should write to.  Scans the directory for all existing
    // .vNN.mp4 siblings of the root and returns root + ".v{max+1}.mp4".
    //   First call on a fresh root   →  "foo_imported.v01.mp4"
    //   After v01..v05 exist         →  "foo_imported.v06.mp4"
    //   After v01, v03, v05 (gaps)   →  "foo_imported.v06.mp4"  (max+1, never fills gaps)
    QString nextVersionPath(const QString& anyVideoPath);

    // List every versioned sibling of the given path's root, sorted by version
    // number ascending.  Does NOT include the root itself.  Returns absolute
    // paths.  Empty list if no versions exist or the directory is inaccessible.
    QStringList listVersions(const QString& anyVideoPath);

    // Sidecar JSON path alongside a video.
    //   "foo_imported.v01.mp4"  →  "foo_imported.v01.mp4.json"
    QString sidecarJsonPath(const QString& videoPath);

    // Convenience: given the currently-loaded video path, return the path of
    // the previous version in the same family — for Ctrl+Z "undo" behaviour.
    // Returns empty QString if no earlier version exists (current is root,
    // current is v01 and root wasn't preserved, etc.).
    //   current = v03  →  v02 if it exists, else v01 if it exists, else root
    //   current = root →  ""  (can't go back before the source import)
    QString previousVersionPath(const QString& currentVideoPath);

}  // namespace VersionPathUtil
