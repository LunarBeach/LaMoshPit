#include "gui/sequencer/SequencerTrackHeader.h"
#include "gui/sequencer/SequencerTimelineConstants.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/EditCommand.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QMouseEvent>

namespace sequencer {

// ── Private row widget — one per track ────────────────────────────────────
namespace {

class TrackRowWidget : public QWidget {
public:
    TrackRowWidget(int trackNumber, const QString& name,
                   bool isActive, QWidget* parent)
        : QWidget(parent), m_trackNumber(trackNumber)
    {
        setFixedHeight(kTrackHeight + kTrackGap);
        setAutoFillBackground(true);

        auto* hl = new QHBoxLayout(this);
        hl->setContentsMargins(6, 2, 6, 2);
        hl->setSpacing(6);

        m_numLabel = new QLabel(QString::number(trackNumber + 1), this);
        m_numLabel->setFixedWidth(18);
        m_numLabel->setAlignment(Qt::AlignCenter);
        m_numLabel->setStyleSheet(
            "QLabel { background:#222; color:#ddd; border-radius:3px; "
            "font:bold 10pt 'Consolas'; padding:2px; }");

        m_nameLabel = new QLabel(name, this);
        m_nameLabel->setStyleSheet("QLabel { color:#ddd; font:9pt 'Segoe UI'; }");
        m_nameLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        hl->addWidget(m_numLabel);
        hl->addWidget(m_nameLabel, /*stretch=*/1);

        setActive(isActive);
    }

    void setActive(bool on)
    {
        // Highlight background when this is the live track.  The dock's
        // preview mirrors whichever track is active.
        setStyleSheet(on
            ? "QWidget { background:#2b3a55; border-left:3px solid #4fa0ff; }"
            : "QWidget { background:#181818; border-left:3px solid transparent; }");
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) emit selectedNotifier();
        QWidget::mousePressEvent(e);
    }

    // Tiny Qt quirk: we want a signal but QObject::emit needs Q_OBJECT +
    // moc.  Keep it simple — use a std::function callback set by parent.
public:
    void setSelectedCallback(std::function<void(int)> cb)
    { m_selectedCb = std::move(cb); }

private:
    void emitSelected() { if (m_selectedCb) m_selectedCb(m_trackNumber); }
    // A minimal "signal" — when mousePressEvent fires, we call the cb.
    void selectedNotifier() { emitSelected(); }

    int       m_trackNumber { 0 };
    QLabel*   m_numLabel    { nullptr };
    QLabel*   m_nameLabel   { nullptr };
    std::function<void(int)> m_selectedCb;
};

} // namespace

// =============================================================================
// SequencerTrackHeader
// =============================================================================

SequencerTrackHeader::SequencerTrackHeader(SequencerProject* project,
                                           QWidget* parent)
    : QWidget(parent), m_project(project)
{
    setFixedWidth(140);
    setAutoFillBackground(true);
    setStyleSheet("QWidget { background:#101010; }");

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Top spacer matching the scene's ruler band so rows align with tracks.
    auto* rulerSpacer = new QFrame(this);
    rulerSpacer->setFixedHeight(kRulerHeight);
    rulerSpacer->setStyleSheet("QFrame { background:#1a1a1a; border-bottom:1px solid #2a2a2a; }");
    m_layout->addWidget(rulerSpacer);

    m_rowsHost   = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);
    m_layout->addWidget(m_rowsHost);

    m_layout->addStretch(1);

    m_btnAddTrack = new QPushButton("+ Track", this);
    m_btnAddTrack->setStyleSheet(
        "QPushButton { background:#222; color:#ddd; border:1px solid #333; "
        "border-radius:3px; padding:4px; font:bold 9pt 'Segoe UI'; }"
        "QPushButton:hover { background:#2e2e2e; }"
        "QPushButton:disabled { color:#555; }");
    m_layout->addWidget(m_btnAddTrack);

    connect(m_btnAddTrack, &QPushButton::clicked,
            this, &SequencerTrackHeader::onAddTrackClicked);

    if (m_project) {
        connect(m_project, &SequencerProject::projectChanged,
                this,      &SequencerTrackHeader::onProjectChanged);
        connect(m_project, &SequencerProject::activeTrackChanged,
                this,      &SequencerTrackHeader::onActiveTrackChanged);
        m_activeTrack = m_project->activeTrackIndex();
    }
    rebuildRows();
}

QSize SequencerTrackHeader::sizeHint() const { return QSize(140, 400); }

void SequencerTrackHeader::onProjectChanged() { rebuildRows(); }

void SequencerTrackHeader::onActiveTrackChanged(int idx)
{
    m_activeTrack = idx;
    rebuildRows();
}

void SequencerTrackHeader::onAddTrackClicked()
{
    if (!m_project) return;
    if (m_project->trackCount() >= SequencerProject::MaxTracks) return;
    const QString name = QString("Track %1")
                         .arg(m_project->trackCount() + 1);
    m_project->executeCommand(std::make_unique<AddTrackCmd>(name));
}

void SequencerTrackHeader::rebuildRows()
{
    // Nuke and rebuild — track lists are short (≤ 9).
    while (auto* child = m_rowsLayout->takeAt(0)) {
        if (auto* w = child->widget()) w->deleteLater();
        delete child;
    }
    if (!m_project) return;

    // Iterate in reverse so the highest-index track (topmost compositor
    // layer) appears at the visual top of the header column — matching the
    // flipped-Y layout in the timeline view.  Track 0 (bottom of the layer
    // stack) ends up at the bottom, mirroring the "look down at the stack"
    // NLE convention.
    const int n = m_project->trackCount();
    timelineTrackCountRef() = std::max(n, 1);
    for (int i = n - 1; i >= 0; --i) {
        const auto& tr = m_project->track(i);
        auto* row = new TrackRowWidget(i, tr.name, i == m_activeTrack, m_rowsHost);
        row->setSelectedCallback([this](int trackIdx) {
            if (m_project) m_project->setActiveTrackIndex(trackIdx);
            emit trackSelected(trackIdx);
        });
        m_rowsLayout->addWidget(row);
    }

    m_btnAddTrack->setEnabled(m_project->trackCount() < SequencerProject::MaxTracks);
}

} // namespace sequencer
