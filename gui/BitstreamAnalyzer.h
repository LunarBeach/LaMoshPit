#pragma once
#include <QString>
#include <QVector>
#include <QByteArray>
#include <QDebug>

struct BasicNALInfo {
    int nalIndex;
    int nalType;
    qint64 fileOffset;
    int size;
};

class BitstreamAnalyzer {
public:
    struct AnalysisResult {
        bool success;
        QString errorMessage;
        QVector<BasicNALInfo> nalUnits;
        QString codecInfo;
    };
    
    static AnalysisResult analyzeVideo(const QString &videoPath);
    static void printAnalysis(const AnalysisResult &result);
    static void saveAnalysisToFile(const AnalysisResult &result, const QString &videoPath); // Add this line
};

