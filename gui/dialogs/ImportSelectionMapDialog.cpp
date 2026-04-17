#include "ImportSelectionMapDialog.h"

#include "core/model/SelectionMap.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QMessageBox>
#include <QApplication>

// =============================================================================
// Small helper: probe a clip quickly and return its meta.  The dialog uses
// this to filter the dropdown.  Results are cached to avoid re-probing
// every clip on every map change.
// =============================================================================
static VideoMeta probeCached(const QString& path, QMap<QString, VideoMeta>& cache)
{
    auto it = cache.find(path);
    if (it != cache.end()) return it.value();
    VideoMeta m = VideoMetaProbe::probe(path);
    cache.insert(path, m);
    return m;
}

// =============================================================================
// ImportSelectionMapDialog
// =============================================================================
ImportSelectionMapDialog::ImportSelectionMapDialog(
    const QString& projectMoshVideoFolder,
    const QString& projectMapsDir,
    const QString& preselectedClipPath,
    QWidget* parent)
    : QDialog(parent),
      m_moshVideoFolder(projectMoshVideoFolder),
      m_mapsDir(projectMapsDir),
      m_preselectedClip(preselectedClipPath)
{
    setWindowTitle("Import Selection Map");
    setModal(true);
    setMinimumWidth(520);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }"
                  "QLineEdit, QComboBox { background:#222; color:#ddd; "
                  "  border:1px solid #333; padding:3px; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        "Import a black-and-white video as a selection map for a clip.\n"
        "Each 16\u00d716 macroblock becomes SELECTED if its mean luma > 127.",
        this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    // ── Map source row ────────────────────────────────────────────────────
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    int r = 0;

    auto* lblMap = new QLabel("Map video:", this);
    m_edMapPath  = new QLineEdit(this);
    m_edMapPath->setReadOnly(true);
    m_btnBrowse  = new QPushButton("Browse\u2026", this);
    grid->addWidget(lblMap,       r, 0);
    grid->addWidget(m_edMapPath,  r, 1);
    grid->addWidget(m_btnBrowse,  r, 2);
    ++r;

    m_lblMapMeta = new QLabel("(no map selected)", this);
    m_lblMapMeta->setStyleSheet("color:#888; font:7pt 'Consolas';");
    grid->addWidget(m_lblMapMeta, r, 1, 1, 2);
    ++r;

    // ── Display name ──────────────────────────────────────────────────────
    auto* lblName = new QLabel("Name:", this);
    m_edName = new QLineEdit(this);
    m_edName->setPlaceholderText("(display name shown in the Apply Map dropdown)");
    grid->addWidget(lblName,  r, 0);
    grid->addWidget(m_edName, r, 1, 1, 2);
    ++r;

    // ── Target clip dropdown ──────────────────────────────────────────────
    auto* lblClip = new QLabel("Associate with clip:", this);
    m_cmbClip = new QComboBox(this);
    grid->addWidget(lblClip,   r, 0);
    grid->addWidget(m_cmbClip, r, 1, 1, 2);
    ++r;

    m_lblClipMeta = new QLabel(QString(), this);
    m_lblClipMeta->setStyleSheet("color:#888; font:7pt 'Consolas';");
    grid->addWidget(m_lblClipMeta, r, 1, 1, 2);
    ++r;

    root->addLayout(grid);

    m_btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_btnBox);

    connect(m_btnBrowse, &QPushButton::clicked,
            this, &ImportSelectionMapDialog::onBrowseMap);
    connect(m_cmbClip,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImportSelectionMapDialog::onClipIndexChanged);
    connect(m_btnBox, &QDialogButtonBox::accepted,
            this, &ImportSelectionMapDialog::onAccept);
    connect(m_btnBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    refreshClipDropdown();
    updateAcceptEnabled();
}

// =============================================================================
// Build the clip dropdown from MoshVideoFolder/ and tag incompatible entries.
// =============================================================================
void ImportSelectionMapDialog::refreshClipDropdown()
{
    // Preserve the current selection (by path) across rebuilds.
    QString keep = m_preselectedClip;
    if (keep.isEmpty() && m_cmbClip->currentIndex() >= 0) {
        const int idx = m_cmbClip->currentIndex();
        if (idx < m_clipPaths.size()) keep = m_clipPaths[idx];
    }

    m_cmbClip->blockSignals(true);
    m_cmbClip->clear();
    m_clipPaths.clear();

    static QMap<QString, VideoMeta> s_cache;

    // Enumerate all .mp4 files in MoshVideoFolder/ (non-recursive).
    QDir dir(m_moshVideoFolder);
    const QStringList files = dir.entryList(
        QStringList() << "*.mp4", QDir::Files, QDir::Name);

    auto* model = qobject_cast<QStandardItemModel*>(m_cmbClip->model());

    for (const QString& fn : files) {
        const QString abs = QDir::cleanPath(dir.filePath(fn));
        m_clipPaths.append(abs);
        m_cmbClip->addItem(fn);

        // Evaluate compatibility if we already know the map's meta.
        bool enabled = true;
        QString suffix;
        if (m_mapMeta.ok) {
            const VideoMeta cm = probeCached(abs, s_cache);
            if (!cm.ok) {
                enabled = false;
                suffix  = "  (unreadable)";
            } else if (!VideoMetaProbe::compatible(m_mapMeta, cm)) {
                enabled = false;
                suffix = QString("  (%1\u00d7%2 %3fps %4f — incompatible)")
                    .arg(cm.width).arg(cm.height)
                    .arg(cm.fps, 0, 'f', 2).arg(cm.totalFrames);
            }
        }

        if (!suffix.isEmpty()) {
            const int row = m_cmbClip->count() - 1;
            m_cmbClip->setItemText(row, fn + suffix);
        }

        if (model) {
            QStandardItem* item = model->item(m_cmbClip->count() - 1);
            if (item) {
                item->setEnabled(enabled);
                if (!enabled) item->setForeground(QBrush(QColor("#555")));
            }
        }
    }

    // Restore selection if possible.
    int selIdx = -1;
    for (int i = 0; i < m_clipPaths.size(); ++i) {
        if (m_clipPaths[i] == keep) { selIdx = i; break; }
    }
    if (selIdx < 0) {
        // Pick the first enabled entry.
        for (int i = 0; i < m_clipPaths.size(); ++i) {
            QStandardItem* item = model ? model->item(i) : nullptr;
            if (!item || item->isEnabled()) { selIdx = i; break; }
        }
    }
    if (selIdx >= 0) m_cmbClip->setCurrentIndex(selIdx);
    m_cmbClip->blockSignals(false);

    if (!m_preselectedClip.isEmpty()) {
        m_cmbClip->setEnabled(false);
    }
    onClipIndexChanged(m_cmbClip->currentIndex());
}

void ImportSelectionMapDialog::onBrowseMap()
{
    const QString start = m_edMapPath->text().isEmpty()
                          ? QDir::homePath()
                          : QFileInfo(m_edMapPath->text()).absolutePath();
    const QString picked = QFileDialog::getOpenFileName(
        this, "Select Map Video", start,
        "Video files (*.mp4 *.mov *.mkv *.avi);;All files (*.*)");
    if (picked.isEmpty()) return;

    m_mapPath = picked;
    m_edMapPath->setText(picked);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_mapMeta = VideoMetaProbe::probe(picked);
    QApplication::restoreOverrideCursor();

    if (m_mapMeta.ok) {
        m_lblMapMeta->setText(
            QString("%1\u00d7%2  %3 fps  %4 frames")
                .arg(m_mapMeta.width).arg(m_mapMeta.height)
                .arg(m_mapMeta.fps, 0, 'f', 2)
                .arg(m_mapMeta.totalFrames));
        m_lblMapMeta->setStyleSheet("color:#00ff88; font:7pt 'Consolas';");
    } else {
        m_lblMapMeta->setText("Could not read map video: " + m_mapMeta.errorMsg);
        m_lblMapMeta->setStyleSheet("color:#ff6666; font:7pt 'Consolas';");
    }

    if (m_edName->text().isEmpty()) {
        m_edName->setText(QFileInfo(picked).completeBaseName());
    }

    refreshClipDropdown();
    updateAcceptEnabled();
}

void ImportSelectionMapDialog::onClipIndexChanged(int idx)
{
    if (idx < 0 || idx >= m_clipPaths.size()) {
        m_lblClipMeta->setText(QString());
        updateAcceptEnabled();
        return;
    }
    // Show the selected clip's metadata for user confirmation.
    static QMap<QString, VideoMeta> s_cache;
    const VideoMeta cm = probeCached(m_clipPaths[idx], s_cache);
    if (cm.ok) {
        m_lblClipMeta->setText(
            QString("Clip: %1\u00d7%2  %3 fps  %4 frames")
                .arg(cm.width).arg(cm.height)
                .arg(cm.fps, 0, 'f', 2).arg(cm.totalFrames));
    } else {
        m_lblClipMeta->setText("Clip is unreadable");
    }
    updateAcceptEnabled();
}

void ImportSelectionMapDialog::updateAcceptEnabled()
{
    bool canAccept = false;
    do {
        if (!m_mapMeta.ok) break;
        if (m_edName->text().trimmed().isEmpty()) break;
        const int idx = m_cmbClip->currentIndex();
        if (idx < 0 || idx >= m_clipPaths.size()) break;
        // Entry must be enabled (compatible).
        auto* model = qobject_cast<QStandardItemModel*>(m_cmbClip->model());
        QStandardItem* item = model ? model->item(idx) : nullptr;
        if (item && !item->isEnabled()) break;
        canAccept = true;
    } while (false);

    if (auto* ok = m_btnBox->button(QDialogButtonBox::Ok))
        ok->setEnabled(canAccept);
}

void ImportSelectionMapDialog::onAccept()
{
    const int idx = m_cmbClip->currentIndex();
    if (idx < 0 || idx >= m_clipPaths.size()) { reject(); return; }
    const QString clipPath = m_clipPaths[idx];
    const QString displayName = m_edName->text().trimmed();

    QString err;
    const QString destPath = SelectionMap::copyIntoProject(
        m_mapPath, m_mapsDir, err);
    if (destPath.isEmpty()) {
        QMessageBox::critical(this, "Import Selection Map",
            "Could not copy map into project:\n" + err);
        return;
    }

    SelectionMapEntry entry;
    entry.name    = displayName;
    entry.absPath = destPath;
    if (!SelectionMap::append(clipPath, entry, m_mapsDir, err)) {
        QMessageBox::critical(this, "Import Selection Map",
            "Could not update map sidecar:\n" + err);
        return;
    }

    m_associatedClip = clipPath;
    accept();
}
