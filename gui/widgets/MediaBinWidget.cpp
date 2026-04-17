#include "MediaBinWidget.h"
#include "gui/AppFonts.h"

#include "core/presets/VersionPathUtil.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QInputDialog>
#include <QProcess>
#include <QHeaderView>
#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <QMimeData>
#include <QUrl>

// ── Drag-aware tree subclass ─────────────────────────────────────────────
// QTreeWidget's built-in drag wraps items with its internal mime format —
// useless to external drop targets (like the NLE timeline) that expect
// file:// URLs.  This minimal subclass overrides mimeData() to produce a
// standard text/uri-list carrying the absolute video paths stored in each
// item's UserRole.  Qt handles the rest of the drag machinery.
class BinTree : public QTreeWidget {
public:
    explicit BinTree(QWidget* parent = nullptr) : QTreeWidget(parent) {}
protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        auto* mime = new QMimeData;
        QList<QUrl> urls;
        for (QTreeWidgetItem* item : items) {
            if (!item) continue;
            const QString path = item->data(0, Qt::UserRole).toString();
            if (path.isEmpty()) continue;
            urls << QUrl::fromLocalFile(path);
        }
        mime->setUrls(urls);
        return mime;
    }
};

namespace {

// User-facing label for a tree item.  Root shows the plain filename; a
// versioned child shows "v03  (2026-04-14 15:34)" with file mtime for quick
// visual scanning through long iteration histories.
QString makeChildLabel(const QFileInfo& fi)
{
    const int v = VersionPathUtil::versionOf(fi.absoluteFilePath());
    return QString("v%1   %2")
        .arg(v, 2, 10, QLatin1Char('0'))
        .arg(fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

// Human-readable file size, e.g. "34 MB" / "1.2 GB".
QString prettyBytes(qint64 b)
{
    if (b < 1024)              return QString("%1 B").arg(b);
    if (b < 1024LL * 1024)     return QString("%1 KB").arg(b / 1024);
    if (b < 1024LL * 1024 * 1024) return QString("%1 MB").arg(b / (1024LL * 1024));
    return QString("%1 GB").arg(double(b) / (1024.0 * 1024.0 * 1024.0),
                                0, 'f', 2);
}

}  // namespace

// =============================================================================

MediaBinWidget::MediaBinWidget(const QString& moshVideoFolder,
                               const QString& thumbnailsDir,
                               QWidget* parent)
    : QWidget(parent)
    , m_moshVideoFolder(moshVideoFolder)
    , m_thumbnailsDir(thumbnailsDir)
{
    setObjectName("MediaBinWidget");
    setStyleSheet(QString(
        "QWidget#MediaBinWidget { background:#121212; }"
        "QLabel { color:#cccccc; font-family:'%1'; font-size:10pt; }"
        "QPushButton { background:#1a1a1a; color:#cccccc; border:1px solid #333; "
                      "border-radius:3px; padding:6px 14px; "
                      "font-family:'%2','%1'; font-size:10pt; }"
        "QPushButton:hover { background:#242424; border-color:#555; }"
        "QTreeWidget { background:#0d0d0d; color:#cfcfcf; "
                     "border:1px solid #2a2a2a; font-family:'%1'; font-size:10pt; }"
        "QTreeWidget::item:selected { background:#1f3a5a; color:#ffffff; }"
    ).arg(AppFonts::bodyFamily(), AppFonts::displayFamily()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Panel heading removed — the containing dock's title bar already shows
    // "Media Bin", so an in-panel heading would duplicate it.

    // Button row: Import (triggers the same openFile() MainWindow uses) and
    // Refresh (rescan the folder — used after manual file-system changes).
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    m_btnImport  = new QPushButton("Import\u2026", this);
    m_btnRefresh = new QPushButton("Refresh",    this);
    m_btnImport->setToolTip("Import a new video (same as File \u2192 Open)");
    m_btnRefresh->setToolTip("Rescan the MoshVideoFolder");
    btnRow->addWidget(m_btnImport);
    btnRow->addWidget(m_btnRefresh);
    btnRow->addStretch(1);
    layout->addLayout(btnRow);

    // Tree: two columns — name + size.  Root shows filename; child shows
    // "vNN   mtime".  Column widths auto-fit; tree expands by default.
    m_tree = new BinTree(this);
    m_tree->setHeaderLabels(QStringList{ "Video", "Size" });
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Drag out to the NLE timeline.  BinTree supplies file:// URLs via its
    // mimeData() override so external drop targets can pick them up.
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    // Icon size: fits 160x90 thumbnails comfortably without swallowing row
    // height.  The tree's stylesheet above draws plenty of vertical padding
    // around the icon; this just tells Qt how big to draw the QIcon itself.
    m_tree->setIconSize(QSize(96, 54));
    layout->addWidget(m_tree, 1);

    // Import button emits a signal MainWindow routes to its openFile() slot.
    connect(m_btnImport, &QPushButton::clicked,
            this, &MediaBinWidget::importRequested);
    connect(m_btnRefresh, &QPushButton::clicked,
            this, &MediaBinWidget::onRefreshClicked);
    connect(m_tree, &QTreeWidget::itemActivated,
            this, &MediaBinWidget::onItemActivated);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &MediaBinWidget::onContextMenuRequested);

    refresh();
}

// =============================================================================

void MediaBinWidget::refresh()
{
    m_tree->clear();

    const QDir dir(m_moshVideoFolder);
    if (!dir.exists()) {
        auto* placeholder = new QTreeWidgetItem(m_tree);
        placeholder->setText(0, "(MoshVideoFolder not found)");
        placeholder->setFlags(Qt::NoItemFlags);
        return;
    }

    // List every .mp4 in the folder, then split into roots (no .vNN suffix)
    // vs children (matches .vNN pattern).  Roots become top-level items;
    // children attach under their matching root.  Children whose root is
    // missing from disk still show up under a synthetic "(orphan)" root so
    // the user can see / delete them.
    const QStringList allMp4 = dir.entryList(
        QStringList{ QStringLiteral("*.mp4") },
        QDir::Files, QDir::Name);

    QMap<QString, QTreeWidgetItem*> rootsByAbsPath;
    QList<QString> orphanChildren;

    // Helper: look up a thumbnail PNG for a video if thumbnailsDir is set.
    // Returns a default-constructed QIcon (falsy) when there's no thumbnail.
    auto iconFor = [this](const QString& videoAbsPath) -> QIcon {
        if (m_thumbnailsDir.isEmpty()) return QIcon();
        const QString png = QDir(m_thumbnailsDir).filePath(
            QFileInfo(videoAbsPath).fileName() + ".png");
        if (!QFile::exists(png)) return QIcon();
        const QPixmap pm(png);
        if (pm.isNull()) return QIcon();
        return QIcon(pm);
    };

    // First pass: create a top-level item for every root file on disk.
    for (const QString& name : allMp4) {
        const QString abs = dir.absoluteFilePath(name);
        if (VersionPathUtil::versionOf(abs) != 0) continue;   // skip children this pass
        const QFileInfo fi(abs);

        auto* rootItem = new QTreeWidgetItem(m_tree);
        rootItem->setText(0, fi.fileName());
        rootItem->setText(1, prettyBytes(fi.size()));
        rootItem->setData(0, Qt::UserRole, abs);
        rootItem->setToolTip(0, abs);
        rootItem->setIcon(0, iconFor(abs));
        rootsByAbsPath.insert(abs, rootItem);
    }

    // Second pass: attach children to their roots (or defer to orphan list).
    for (const QString& name : allMp4) {
        const QString abs = dir.absoluteFilePath(name);
        if (VersionPathUtil::versionOf(abs) == 0) continue;   // skip roots this pass

        const QString rootAbs = VersionPathUtil::rootPath(abs);
        QTreeWidgetItem* rootItem = rootsByAbsPath.value(rootAbs, nullptr);

        if (!rootItem) {
            // Root was deleted but versioned children survive — make a stub
            // root entry so the orphaned iterations are still visible and
            // manageable from the UI.
            const QFileInfo rfi(rootAbs);
            rootItem = new QTreeWidgetItem(m_tree);
            rootItem->setText(0, rfi.fileName() + "  (source missing)");
            rootItem->setData(0, Qt::UserRole, rootAbs);
            rootItem->setDisabled(true);   // nothing to load for the stub
            rootsByAbsPath.insert(rootAbs, rootItem);
        }

        const QFileInfo fi(abs);
        auto* child = new QTreeWidgetItem(rootItem);
        child->setText(0, makeChildLabel(fi));
        child->setText(1, prettyBytes(fi.size()));
        child->setData(0, Qt::UserRole, abs);
        child->setToolTip(0, abs);
        child->setIcon(0, iconFor(abs));
    }

    m_tree->expandAll();
}

// =============================================================================

void MediaBinWidget::setCurrentVideoPath(const QString& absPath)
{
    // Iterate top-level roots and their children searching for absPath.
    // Select + ensure visible if found.  Silent no-op otherwise.
    const int rootCount = m_tree->topLevelItemCount();
    for (int r = 0; r < rootCount; ++r) {
        QTreeWidgetItem* root = m_tree->topLevelItem(r);
        if (pathOf(root) == absPath) {
            m_tree->setCurrentItem(root);
            m_tree->scrollToItem(root);
            return;
        }
        for (int c = 0; c < root->childCount(); ++c) {
            QTreeWidgetItem* child = root->child(c);
            if (pathOf(child) == absPath) {
                m_tree->setCurrentItem(child);
                m_tree->scrollToItem(child);
                return;
            }
        }
    }
}

// =============================================================================

void MediaBinWidget::onItemActivated(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || item->isDisabled()) return;
    const QString path = pathOf(item);
    if (path.isEmpty() || !QFile::exists(path)) return;
    emit videoSelected(path);
}

// =============================================================================

void MediaBinWidget::onContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;
    const QString absPath = pathOf(item);
    if (absPath.isEmpty()) return;

    const bool isRoot    = (item->parent() == nullptr);
    const bool exists    = QFile::exists(absPath);

    QMenu menu(this);

    QAction* actLoad   = menu.addAction("Load");
    actLoad->setEnabled(exists && !item->isDisabled());

    menu.addSeparator();

    QAction* actReveal = menu.addAction("Reveal in Explorer");
    actReveal->setEnabled(exists);

    menu.addSeparator();

    QAction* actDelete = menu.addAction("Delete this file");
    actDelete->setEnabled(exists);

    QAction* actDeleteAll = nullptr;
    if (isRoot) {
        actDeleteAll = menu.addAction("Delete all versions (root + iterations)");
    }

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actLoad) {
        emit videoSelected(absPath);
    }
    else if (chosen == actReveal) {
        revealInExplorer(absPath);
    }
    else if (chosen == actDelete) {
        // Confirmation: for roots, explain that iterations remain on disk as
        // orphans; for versioned children, it's just that file.
        QString msg;
        if (isRoot) {
            msg = QString("Delete the source file \"%1\"?\n\n"
                          "Any versioned iterations will remain on disk and "
                          "appear under a \"(source missing)\" entry.")
                  .arg(QFileInfo(absPath).fileName());
        } else {
            msg = QString("Delete \"%1\"?\nThis cannot be undone.")
                  .arg(QFileInfo(absPath).fileName());
        }
        if (QMessageBox::question(this, "Confirm Delete", msg,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes) {
            deleteFileWithSidecar(absPath);
            refresh();
        }
    }
    else if (actDeleteAll && chosen == actDeleteAll) {
        const QStringList versions = VersionPathUtil::listVersions(absPath);
        const int n = versions.size() + (exists ? 1 : 0);
        const QString msg = QString(
            "Delete \"%1\" AND %2 versioned iteration%3?\n\n"
            "This cannot be undone.")
            .arg(QFileInfo(absPath).fileName())
            .arg(versions.size())
            .arg(versions.size() == 1 ? "" : "s");
        if (QMessageBox::question(this, "Confirm Delete All", msg,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes) {
            for (const QString& v : versions) deleteFileWithSidecar(v);
            if (exists) deleteFileWithSidecar(absPath);
            Q_UNUSED(n);
            refresh();
        }
    }
}

// =============================================================================

void MediaBinWidget::onRefreshClicked()
{
    refresh();
}

// =============================================================================

void MediaBinWidget::revealInExplorer(const QString& absPath)
{
    // Windows shell: /select,"path" highlights the file inside the folder.
    // QProcess::startDetached handles quoting correctly when passed args as
    // a list, but explorer.exe is one of the few programs that wants the
    // /select, flag joined to the path with a comma and no space — which
    // QProcess's list form will NOT produce.  Use the string form here.
    const QString cmd = QString("explorer.exe /select,\"%1\"")
                            .arg(QDir::toNativeSeparators(absPath));
    QProcess::startDetached(cmd);
}

// =============================================================================

bool MediaBinWidget::deleteFileWithSidecar(const QString& absPath)
{
    bool ok = true;
    if (QFile::exists(absPath)) {
        ok = QFile::remove(absPath);
    }
    const QString sidecar = VersionPathUtil::sidecarJsonPath(absPath);
    if (QFile::exists(sidecar)) {
        QFile::remove(sidecar);   // best effort, no error on failure
    }
    return ok;
}

// =============================================================================

QString MediaBinWidget::pathOf(QTreeWidgetItem* item)
{
    if (!item) return QString();
    return item->data(0, Qt::UserRole).toString();
}
