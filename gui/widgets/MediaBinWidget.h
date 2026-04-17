#pragma once

// =============================================================================
// MediaBinWidget — imported-video catalog + render-iteration browser.
//
// Scans the `MoshVideoFolder/` folder (relative to the app's working dir) and
// presents it as a two-level tree:
//
//     foo_imported.mp4              ← root (original import)
//       foo_imported.v01.mp4         ← render iteration 1
//       foo_imported.v02.mp4         ← render iteration 2
//     bar_imported.mp4
//       bar_imported.v01.mp4
//
// Children are identified by the ".vNN.mp4" convention (VersionPathUtil).
// Double-clicking any entry emits videoSelected(absolutePath); MainWindow
// handles the load.
//
// Context menu offers:
//   Load              — same as double-click
//   Rename root…      — renames both the source and all its versioned siblings
//   Reveal in Explorer — shell-opens the folder with the file pre-selected
//   Delete this file  — deletes just the selected video (and sidecar JSON)
//   Delete all versions — deletes every .vNN.mp4 sibling of the root
//
// No auto-prune, no version-limit, no "compact old iterations" — destructive
// actions are only ever explicit clicks.  The user can also manage files via
// the OS file explorer and hit the Refresh button to resync.
// =============================================================================

#include <QWidget>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

class MediaBinWidget : public QWidget {
    Q_OBJECT

public:
    // moshVideoFolder: the project's MoshVideoFolder/ root to scan.
    // thumbnailsDir: optional — if non-empty, tree items look up icons at
    // {thumbnailsDir}/{videoFileName}.png.  Kept pluggable so a future bin
    // could use a different thumbnail location (e.g. AppData cache).
    explicit MediaBinWidget(const QString& moshVideoFolder,
                            const QString& thumbnailsDir = QString(),
                            QWidget* parent = nullptr);

    // Rescan the MoshVideoFolder/ folder and rebuild the tree.  Called on
    // construction, on manual Refresh, after imports, and after renders.
    void refresh();

    // Highlight / scroll to the given video path in the tree (if present).
    // Called by MainWindow when the active video changes so the bin reflects
    // what's loaded.
    void setCurrentVideoPath(const QString& absPath);

signals:
    // Emitted when the user double-clicks an entry or picks Load from the
    // context menu.  MainWindow connects this to its load slot.
    void videoSelected(const QString& absPath);

    // Emitted when the Import button is clicked.  MainWindow connects this
    // to its existing openFile() slot so the bin's button behaves identically
    // to File -> Open in the menu bar.
    void importRequested();

private slots:
    void onItemActivated(QTreeWidgetItem* item, int column);
    void onContextMenuRequested(const QPoint& pos);
    void onRefreshClicked();

private:
    // Shell-open the containing folder with `absPath` highlighted.  Windows-
    // specific (explorer.exe /select,) — on other platforms just opens the
    // parent folder.  Kept local because LaMoshPit is Windows-only today.
    void revealInExplorer(const QString& absPath);

    // Delete a file + its .json sidecar, with confirmation.  Returns true if
    // the file was removed (or didn't exist); false on permission error.
    bool deleteFileWithSidecar(const QString& absPath);

    // Absolute path of the file backing a tree item (stored in UserRole).
    static QString pathOf(QTreeWidgetItem* item);

    QString        m_moshVideoFolder;    // absolute path to MoshVideoFolder/
    QString        m_thumbnailsDir;      // absolute path to thumbnails/ (or empty)
    QTreeWidget*   m_tree       { nullptr };
    QPushButton*   m_btnImport  { nullptr };
    QPushButton*   m_btnRefresh { nullptr };
};
