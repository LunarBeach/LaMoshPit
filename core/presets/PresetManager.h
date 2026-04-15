#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>

#include "core/model/MBEditData.h"
#include "core/model/GlobalEncodeParams.h"

// =============================================================================
// PresetManager — static utility for reading/writing named user presets.
//
// Storage layout (all under QStandardPaths::AppDataLocation):
//   .../presets/mb_editor/<name>.json
//   .../presets/global_encode/<name>.json
//   .../presets/quick_mosh/<name>.json
//
// File formats:
//   MB Editor:     { "presetType":"mb_editor",    "version":1, "params":{...} }
//   Global Encode: { "presetType":"global_encode", "version":1, "params":{...} }
//   Quick Mosh:    { "presetType":"quick_mosh",   "version":1, "mb":{...}, "gp":{...} }
//
// Note: selectedMBs and spatialMaskMBs are intentionally NOT serialised —
// presets capture only the control knob values, not canvas selections.
// =============================================================================
class PresetManager {
public:
    enum class Type { MBEditor, GlobalEncode, QuickMosh };

    // List all preset names for the given type (alphabetically sorted).
    static QStringList list(Type t);

    // ── MB Editor ──────────────────────────────────────────────────────────────
    static bool saveMBEditor(const QString& name, const FrameMBParams& p);
    static bool loadMBEditor(const QString& name, FrameMBParams& out);

    // ── Global Encode ──────────────────────────────────────────────────────────
    static bool saveGlobalEncode(const QString& name, const GlobalEncodeParams& p);
    static bool loadGlobalEncode(const QString& name, GlobalEncodeParams& out);

    // ── Quick Mosh ─────────────────────────────────────────────────────────────
    static bool saveQuickMosh(const QString& name,
                               const FrameMBParams& mb,
                               const GlobalEncodeParams& gp);
    static bool loadQuickMosh(const QString& name,
                               FrameMBParams& mbOut,
                               GlobalEncodeParams& gpOut);

    // ── Management ─────────────────────────────────────────────────────────────
    // Delete the named preset. Returns false if it didn't exist.
    static bool deletePreset(Type t, const QString& name);

    // Import a .json file as a preset with the given name.
    // Returns false if the file's "presetType" field doesn't match t.
    static bool importFile(Type t, const QString& srcPath, const QString& outName);

    // Sanitize a user-supplied name so it can be used as a filename.
    static QString sanitize(const QString& name);

    // ── Public JSON (de)serialization helpers ─────────────────────────────────
    // Exposed so other subsystems (e.g. FrameTransformer writing a per-render
    // sidecar .json next to the versioned MP4 output) can round-trip knob
    // state without reaching into PresetManager.cpp's static internals.
    static QJsonObject      mbToJson(const FrameMBParams& p);
    static FrameMBParams    jsonToMB(const QJsonObject& o);
    static QJsonObject      gpToJson(const GlobalEncodeParams& p);
    static GlobalEncodeParams jsonToGP(const QJsonObject& o);

private:
    static QString presetDir(Type t);
    static QString presetPath(Type t, const QString& name);
};
