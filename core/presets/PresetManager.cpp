#include "PresetManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <algorithm>

// =============================================================================
// Internal helpers
// =============================================================================

static QString subdirName(PresetManager::Type t)
{
    switch (t) {
    case PresetManager::Type::MBEditor:     return QStringLiteral("mb_editor");
    case PresetManager::Type::GlobalEncode: return QStringLiteral("global_encode");
    case PresetManager::Type::QuickMosh:    return QStringLiteral("quick_mosh");
    }
    return QStringLiteral("unknown");
}

static QString expectedPresetType(PresetManager::Type t)
{
    switch (t) {
    case PresetManager::Type::MBEditor:     return QStringLiteral("mb_editor");
    case PresetManager::Type::GlobalEncode: return QStringLiteral("global_encode");
    case PresetManager::Type::QuickMosh:    return QStringLiteral("quick_mosh");
    }
    return QString();
}

// ── JSON: FrameMBParams (no selectedMBs) ─────────────────────────────────────

static QJsonObject mbToJson(const FrameMBParams& p)
{
    QJsonObject o;
    o["qpDelta"]      = p.qpDelta;
    o["mvDriftX"]     = p.mvDriftX;
    o["mvDriftY"]     = p.mvDriftY;
    o["refDepth"]     = p.refDepth;
    o["ghostBlend"]   = p.ghostBlend;
    o["noiseLevel"]   = p.noiseLevel;
    o["pixelOffset"]  = p.pixelOffset;
    o["invertLuma"]   = p.invertLuma;
    o["chromaDriftX"] = p.chromaDriftX;
    o["chromaDriftY"] = p.chromaDriftY;
    o["chromaOffset"] = p.chromaOffset;
    o["spillRadius"]  = p.spillRadius;
    o["sampleRadius"] = p.sampleRadius;
    o["mvAmplify"]    = p.mvAmplify;
    o["cascadeLen"]   = p.cascadeLen;
    o["cascadeDecay"] = p.cascadeDecay;
    o["blockFlatten"] = p.blockFlatten;
    o["refScatter"]   = p.refScatter;
    o["colorTwistU"]  = p.colorTwistU;
    o["colorTwistV"]  = p.colorTwistV;
    // Pixel-domain additions
    o["posterize"]    = p.posterize;
    o["pixelShuffle"] = p.pixelShuffle;
    o["sharpen"]      = p.sharpen;
    o["tempDiffAmp"]  = p.tempDiffAmp;
    o["hueRotate"]    = p.hueRotate;
    // Bitstream-domain
    o["bsMvdX"]       = p.bsMvdX;
    o["bsMvdY"]       = p.bsMvdY;
    o["bsForceSkip"]  = p.bsForceSkip;
    o["bsIntraMode"]  = p.bsIntraMode;
    o["bsMbType"]     = p.bsMbType;
    o["bsDctScale"]   = p.bsDctScale;
    o["bsCbpZero"]    = p.bsCbpZero;
    return o;
}

static FrameMBParams jsonToMB(const QJsonObject& o)
{
    FrameMBParams p;
    p.qpDelta      = o["qpDelta"].toInt(0);
    p.mvDriftX     = o["mvDriftX"].toInt(0);
    p.mvDriftY     = o["mvDriftY"].toInt(0);
    p.refDepth     = o["refDepth"].toInt(0);
    p.ghostBlend   = o["ghostBlend"].toInt(0);
    p.noiseLevel   = o["noiseLevel"].toInt(0);
    p.pixelOffset  = o["pixelOffset"].toInt(0);
    p.invertLuma   = o["invertLuma"].toInt(0);
    p.chromaDriftX = o["chromaDriftX"].toInt(0);
    p.chromaDriftY = o["chromaDriftY"].toInt(0);
    p.chromaOffset = o["chromaOffset"].toInt(0);
    p.spillRadius  = o["spillRadius"].toInt(0);
    p.sampleRadius = o["sampleRadius"].toInt(0);
    p.mvAmplify    = o["mvAmplify"].toInt(1);
    p.cascadeLen   = o["cascadeLen"].toInt(0);
    p.cascadeDecay = o["cascadeDecay"].toInt(0);
    p.blockFlatten = o["blockFlatten"].toInt(0);
    p.refScatter   = o["refScatter"].toInt(0);
    p.colorTwistU  = o["colorTwistU"].toInt(0);
    p.colorTwistV  = o["colorTwistV"].toInt(0);
    // Pixel-domain additions
    p.posterize    = o["posterize"].toInt(8);
    p.pixelShuffle = o["pixelShuffle"].toInt(0);
    p.sharpen      = o["sharpen"].toInt(0);
    p.tempDiffAmp  = o["tempDiffAmp"].toInt(0);
    p.hueRotate    = o["hueRotate"].toInt(0);
    // Bitstream-domain
    p.bsMvdX       = o["bsMvdX"].toInt(0);
    p.bsMvdY       = o["bsMvdY"].toInt(0);
    p.bsForceSkip  = o["bsForceSkip"].toInt(0);
    p.bsIntraMode  = o["bsIntraMode"].toInt(-1);
    p.bsMbType     = o["bsMbType"].toInt(-1);
    p.bsDctScale   = o["bsDctScale"].toInt(100);
    p.bsCbpZero    = o["bsCbpZero"].toInt(0);
    return p;
}

// ── JSON: GlobalEncodeParams (no spatialMaskMBs) ──────────────────────────────

static QJsonObject gpToJson(const GlobalEncodeParams& p)
{
    QJsonObject o;
    o["gopSize"]       = p.gopSize;
    o["bFrames"]       = p.bFrames;
    o["bAdapt"]        = p.bAdapt;
    o["refFrames"]     = p.refFrames;
    o["qpOverride"]    = p.qpOverride;
    o["qpMin"]         = p.qpMin;
    o["qpMax"]         = p.qpMax;
    o["meMethod"]      = p.meMethod;
    o["meRange"]       = p.meRange;
    o["subpelRef"]     = p.subpelRef;
    // Legacy single-partition field kept for backward compat with old presets
    // (readers that don't know about the new three-field split still round-trip).
    o["partitionMode"] = p.partitionMode;
    // New per-frame-type MB Type dropdowns.
    o["iFrameMbType"]  = p.iFrameMbType;
    o["pFrameMbType"]  = p.pFrameMbType;
    o["bFrameMbType"]  = p.bFrameMbType;
    o["use8x8DCT"]     = p.use8x8DCT;
    o["directMode"]    = p.directMode;
    o["weightedPredB"] = p.weightedPredB;
    o["weightedPredP"] = p.weightedPredP;
    o["trellis"]       = p.trellis;
    o["noFastPSkip"]   = p.noFastPSkip;
    o["noDctDecimate"] = p.noDctDecimate;
    o["cabacDisable"]  = p.cabacDisable;
    o["noDeblock"]     = p.noDeblock;
    o["deblockAlpha"]  = p.deblockAlpha;
    o["deblockBeta"]   = p.deblockBeta;
    o["psyRD"]         = (double)p.psyRD;
    o["psyTrellis"]    = (double)p.psyTrellis;
    o["aqMode"]        = p.aqMode;
    o["aqStrength"]    = (double)p.aqStrength;
    o["mbTreeDisable"] = p.mbTreeDisable;
    o["killIFrames"]   = p.killIFrames;
    o["scenecut"]      = p.scenecut;
    o["rcLookahead"]   = p.rcLookahead;
    o["spatialMaskQP"] = p.spatialMaskQP;
    // Rate-control fidelity
    o["qcompEnabled"]          = p.qcompEnabled;
    o["qcomp"]                 = (double)p.qcomp;
    o["ipratioEnabled"]        = p.ipratioEnabled;
    o["ipratio"]               = (double)p.ipratio;
    o["pbratioEnabled"]        = p.pbratioEnabled;
    o["pbratio"]               = (double)p.pbratio;
    o["deadzoneInterEnabled"]  = p.deadzoneInterEnabled;
    o["deadzoneInter"]         = p.deadzoneInter;
    o["deadzoneIntraEnabled"]  = p.deadzoneIntraEnabled;
    o["deadzoneIntra"]         = p.deadzoneIntra;
    o["qblurEnabled"]          = p.qblurEnabled;
    o["qblur"]                 = (double)p.qblur;
    return o;
}

static GlobalEncodeParams jsonToGP(const QJsonObject& o)
{
    GlobalEncodeParams p;
    p.gopSize       = o["gopSize"].toInt(-1);
    p.bFrames       = o["bFrames"].toInt(-1);
    p.bAdapt        = o["bAdapt"].toInt(-1);
    p.refFrames     = o["refFrames"].toInt(-1);
    p.qpOverride    = o["qpOverride"].toInt(-1);
    p.qpMin         = o["qpMin"].toInt(-1);
    p.qpMax         = o["qpMax"].toInt(-1);
    p.meMethod      = o["meMethod"].toInt(-1);
    p.meRange       = o["meRange"].toInt(-1);
    p.subpelRef     = o["subpelRef"].toInt(-1);
    p.partitionMode = o["partitionMode"].toInt(-1);  // legacy, no longer read by render
    // New per-frame-type MB Type dropdowns — default -1 when absent so old
    // presets written before the split seamlessly upgrade to "natural" behaviour.
    p.iFrameMbType  = o["iFrameMbType"].toInt(-1);
    p.pFrameMbType  = o["pFrameMbType"].toInt(-1);
    p.bFrameMbType  = o["bFrameMbType"].toInt(-1);
    p.use8x8DCT     = o["use8x8DCT"].toBool(true);
    p.directMode    = o["directMode"].toInt(-1);
    p.weightedPredB = o["weightedPredB"].toBool(true);
    p.weightedPredP = o["weightedPredP"].toInt(-1);
    p.trellis       = o["trellis"].toInt(-1);
    p.noFastPSkip   = o["noFastPSkip"].toBool(false);
    p.noDctDecimate = o["noDctDecimate"].toBool(false);
    p.cabacDisable  = o["cabacDisable"].toBool(false);
    p.noDeblock     = o["noDeblock"].toBool(false);
    p.deblockAlpha  = o["deblockAlpha"].toInt(0);
    p.deblockBeta   = o["deblockBeta"].toInt(0);
    p.psyRD         = (float)o["psyRD"].toDouble(-1.0);
    p.psyTrellis    = (float)o["psyTrellis"].toDouble(-1.0);
    p.aqMode        = o["aqMode"].toInt(-1);
    p.aqStrength    = (float)o["aqStrength"].toDouble(-1.0);
    p.mbTreeDisable = o["mbTreeDisable"].toBool(false);
    p.killIFrames   = o["killIFrames"].toBool(false);
    p.scenecut      = o["scenecut"].toBool(false);
    p.rcLookahead   = o["rcLookahead"].toInt(-1);
    p.spatialMaskQP = o["spatialMaskQP"].toInt(51);
    // Rate-control fidelity
    p.qcompEnabled          = o["qcompEnabled"].toBool(false);
    p.qcomp                 = (float)o["qcomp"].toDouble(0.6);
    p.ipratioEnabled        = o["ipratioEnabled"].toBool(false);
    p.ipratio               = (float)o["ipratio"].toDouble(1.4);
    p.pbratioEnabled        = o["pbratioEnabled"].toBool(false);
    p.pbratio               = (float)o["pbratio"].toDouble(1.3);
    p.deadzoneInterEnabled  = o["deadzoneInterEnabled"].toBool(false);
    p.deadzoneInter         = o["deadzoneInter"].toInt(21);
    p.deadzoneIntraEnabled  = o["deadzoneIntraEnabled"].toBool(false);
    p.deadzoneIntra         = o["deadzoneIntra"].toInt(11);
    p.qblurEnabled          = o["qblurEnabled"].toBool(false);
    p.qblur                 = (float)o["qblur"].toDouble(0.5);
    return p;
}

// =============================================================================
// PresetManager implementation
// =============================================================================

QString PresetManager::presetDir(Type t)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/presets/") + subdirName(t);
}

QString PresetManager::presetPath(Type t, const QString& name)
{
    return presetDir(t) + QStringLiteral("/") + sanitize(name) + QStringLiteral(".json");
}

QString PresetManager::sanitize(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar& c : name) {
        if (c.isLetterOrNumber() || c == QLatin1Char(' ') ||
            c == QLatin1Char('-') || c == QLatin1Char('_') ||
            c == QLatin1Char('(') || c == QLatin1Char(')') ||
            c == QLatin1Char('[') || c == QLatin1Char(']') ||
            c == QLatin1Char('.'))
            out += c;
        else
            out += QLatin1Char('_');
    }
    return out.trimmed();
}

QStringList PresetManager::list(Type t)
{
    const QDir dir(presetDir(t));
    if (!dir.exists()) return {};
    const QStringList files = dir.entryList(QStringList{QStringLiteral("*.json")},
                                             QDir::Files, QDir::Name);
    QStringList names;
    names.reserve(files.size());
    for (const QString& f : files)
        names << QFileInfo(f).completeBaseName();
    return names;
}

// ── MB Editor ─────────────────────────────────────────────────────────────────

bool PresetManager::saveMBEditor(const QString& name, const FrameMBParams& p)
{
    QDir().mkpath(presetDir(Type::MBEditor));
    QJsonObject root;
    root["presetType"] = QStringLiteral("mb_editor");
    root["version"]    = 1;
    root["name"]       = name;
    root["params"]     = mbToJson(p);
    QFile f(presetPath(Type::MBEditor, name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool PresetManager::loadMBEditor(const QString& name, FrameMBParams& out)
{
    QFile f(presetPath(Type::MBEditor, name));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root["presetType"].toString() != QLatin1String("mb_editor")) return false;
    out = jsonToMB(root["params"].toObject());
    return true;
}

// ── Global Encode ─────────────────────────────────────────────────────────────

bool PresetManager::saveGlobalEncode(const QString& name, const GlobalEncodeParams& p)
{
    QDir().mkpath(presetDir(Type::GlobalEncode));
    QJsonObject root;
    root["presetType"] = QStringLiteral("global_encode");
    root["version"]    = 1;
    root["name"]       = name;
    root["params"]     = gpToJson(p);
    QFile f(presetPath(Type::GlobalEncode, name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool PresetManager::loadGlobalEncode(const QString& name, GlobalEncodeParams& out)
{
    QFile f(presetPath(Type::GlobalEncode, name));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root["presetType"].toString() != QLatin1String("global_encode")) return false;
    out = jsonToGP(root["params"].toObject());
    return true;
}

// ── Quick Mosh ────────────────────────────────────────────────────────────────

bool PresetManager::saveQuickMosh(const QString& name,
                                   const FrameMBParams& mb,
                                   const GlobalEncodeParams& gp)
{
    QDir().mkpath(presetDir(Type::QuickMosh));
    QJsonObject root;
    root["presetType"] = QStringLiteral("quick_mosh");
    root["version"]    = 1;
    root["name"]       = name;
    root["mb"]         = mbToJson(mb);
    root["gp"]         = gpToJson(gp);
    QFile f(presetPath(Type::QuickMosh, name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool PresetManager::loadQuickMosh(const QString& name,
                                   FrameMBParams& mbOut,
                                   GlobalEncodeParams& gpOut)
{
    QFile f(presetPath(Type::QuickMosh, name));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root["presetType"].toString() != QLatin1String("quick_mosh")) return false;
    mbOut = jsonToMB(root["mb"].toObject());
    gpOut = jsonToGP(root["gp"].toObject());
    return true;
}

// ── Management ────────────────────────────────────────────────────────────────

bool PresetManager::deletePreset(Type t, const QString& name)
{
    return QFile::remove(presetPath(t, name));
}

bool PresetManager::importFile(Type t, const QString& srcPath, const QString& outName)
{
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(src.readAll()).object();
    src.close();

    if (root["presetType"].toString() != expectedPresetType(t)) return false;

    QDir().mkpath(presetDir(t));
    const QString dest = presetPath(t, outName);
    QFile::remove(dest);   // overwrite if exists
    QFile srcCopy(srcPath);
    return srcCopy.copy(dest);
}
