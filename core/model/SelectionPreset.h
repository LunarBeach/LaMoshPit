#pragma once

#include <QString>
#include <QDateTime>
#include <QSet>
#include <QMap>
#include <QList>

// =============================================================================
// SelectionPreset
//
// A shareable pattern of MB selections spanning one or more frames.  Offset 0
// is the preset's "anchor" — at load time the user's current frame receives
// offset 0, the next frame offset 1, etc.  Frames whose offsets are absent
// from `frames` are empty selections.
//
// Saved as JSON in
//   {AppDataLocation}/selection_presets/{sanitized_name}.json
//
// so presets persist across projects and can be shared by copying the file.
// A preset also carries the MB grid (mbCols/mbRows) it was captured on; at
// load time the target clip must match or the preset is rejected.
// =============================================================================
struct SelectionPreset {
    QString              name;
    QDateTime            created;
    int                  mbCols     = 0;
    int                  mbRows     = 0;
    int                  frameCount = 0;  // span: offsets 0..frameCount-1
    QMap<int, QSet<int>> frames;          // sparse: offset → MB set
};

class SelectionPresetIO {
public:
    // Parse a preset file on disk.  Returns false on read/parse errors.
    static bool load(const QString& absPath,
                     SelectionPreset& out,
                     QString& errorMsg);

    // Write a preset as JSON.  Creates the parent directory if missing.
    static bool save(const QString& absPath,
                     const SelectionPreset& preset,
                     QString& errorMsg);

    // Root directory for app-level user presets.  Created on first call.
    static QString userPresetDir();

    // Sanitized path for saving `preset` under userPresetDir() using
    // preset.name.  Filename-unsafe chars are replaced with underscores.
    static QString userPresetPathFor(const QString& presetName);

    // Enumerate all .json files in userPresetDir(), returning absolute
    // paths sorted alphabetically.  Missing dir is tolerated (returns []).
    static QStringList listUserPresets();
};
