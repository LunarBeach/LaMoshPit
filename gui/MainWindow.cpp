#include "MainWindow.h"
#include "widgets/TimelineWidget.h"
#include "widgets/PreviewPlayer.h"
#include "widgets/PropertyPanel.h"
#include "core/pipeline/DecodePipeline.h"
#include "widgets/BitstreamTestWidget.h"
#include "BitstreamAnalyzer.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_videoSequence(new VideoSequence())
{
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);

    m_timeline = new TimelineWidget(this);
    m_preview = new PreviewPlayer(this);
    m_propertyPanel = new PropertyPanel(this);

    auto* contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->addWidget(m_preview);
    contentSplitter->addWidget(m_propertyPanel);
    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 1);

    mainLayout->addWidget(m_timeline, 1);
    mainLayout->addWidget(contentSplitter, 4);
    mainLayout->addWidget(new BitstreamTestWidget(this), 1);

    setCentralWidget(centralWidget);

    // Menu
    auto* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction("Open Video...", this, &MainWindow::openFile);
    fileMenu->addAction("Save Mosh Pit...", this, &MainWindow::saveHacked);

    statusBar()->showMessage("Lee Anne's Mosh Pit — Ready for chaos");
}

MainWindow::~MainWindow() {
    delete m_videoSequence;
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Video File",
        "",
        "Video Files (*.mp4 *.mov *.mkv *.avi *.264 *.h264);;All Files (*.*)");

    if (fileName.isEmpty())
        return;

    statusBar()->showMessage("Importing and standardizing video...");

    // Create "imported_videos" folder if it doesn't exist
    QDir importDir(QDir::currentPath() + "/imported_videos");
    if (!importDir.exists()) {
        importDir.mkpath(".");
    }

    // Generate output filename with .mp4 wrapper
    QString baseName = QFileInfo(fileName).completeBaseName();
    QString outputPath = importDir.absoluteFilePath(baseName + "_imported.mp4");

    // Call the newly separated pipeline!
    bool success = DecodePipeline::standardizeVideo(fileName, outputPath);

    if (success) {
        statusBar()->showMessage("Imported successfully: " + baseName + "_imported.mp4");
        
        // Load the video into the preview player
        m_preview->loadVideo(outputPath);
        m_preview->setStatus("Loaded: " + baseName + "_imported.mp4\nReady for mosh");

        QMessageBox::information(this, "Import Complete",
            "Video successfully standardized for the Mosh Pit.\n\nFile saved as:\n" + outputPath);
        
        m_videoSequence->load(outputPath);
        
        // Analyze the bitstream after successful import
        analyzeImportedVideo(outputPath);
        
    } else {
        statusBar()->showMessage("Import failed.");
        QMessageBox::warning(this, "Import Failed", 
            "Failed to transcode the video stream.");
    }
}

void MainWindow::saveHacked()
{
    QString fileName = QFileDialog::getSaveFileName(this, 
        "Save Hacked Stream", "", "H.264 MP4 Files (*.mp4)");
    
    if (!fileName.isEmpty()) {
        statusBar()->showMessage("Saved hacked stream: " + fileName);
        QMessageBox::information(this, "Saved", "Hacked stream saved (placeholder)");
    }
}

void MainWindow::analyzeImportedVideo(const QString &videoPath) {
    statusBar()->showMessage("Analyzing video bitstream...");
    
    // Direct call instead of QtConcurrent for now to avoid complexity
    BitstreamAnalyzer::AnalysisResult result = BitstreamAnalyzer::analyzeVideo(videoPath);
    
    if (result.success) {
        BitstreamAnalyzer::printAnalysis(result);
        statusBar()->showMessage("Bitstream analysis complete");
        // Save detailed analysis to file
        BitstreamAnalyzer::saveAnalysisToFile(result, videoPath);
           QString analysisSummary = QString("Analysis complete: %1 NAL units found").arg(result.nalUnits.size());
        statusBar()->showMessage(analysisSummary);
        // For now, just print to console - we'll connect to timeline later
        qDebug() << "Analysis completed for:" << videoPath;
    } else {
        statusBar()->showMessage("Bitstream analysis failed: " + result.errorMessage);
        QMessageBox::warning(this, "Analysis Failed", result.errorMessage);
        qDebug() << "Analysis error:" << result.errorMessage;
    }
}
