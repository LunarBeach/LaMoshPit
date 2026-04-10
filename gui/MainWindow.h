#pragma once
#include <QMainWindow>
#include "core/model/VideoSequence.h"

class TimelineWidget;
class PreviewPlayer;
class PropertyPanel;
class VideoSequence;
class BitstreamAnalyzer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

// Remove the problematic signal:
// signals:
//     void videoAnalyzed(const BitstreamAnalyzer::AnalysisResult &result);

private slots:
    void openFile();
    void saveHacked();

private:
    void analyzeImportedVideo(const QString &videoPath);
    bool extractH264Stream(const QString& inputFile, const QString& outputFile);

    TimelineWidget* m_timeline;
    PreviewPlayer* m_preview;
    PropertyPanel* m_propertyPanel;
    
    VideoSequence* m_videoSequence;
};
