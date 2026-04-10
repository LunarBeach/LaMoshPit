#include "BitstreamAnalyzer.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QFileInfo>

BitstreamAnalyzer::AnalysisResult BitstreamAnalyzer::analyzeVideo(const QString &videoPath) {
    AnalysisResult result;
    result.success = false;
    result.codecInfo = "H.264 Bitstream Analyzer";
    
    QFile file(videoPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = "Cannot open file: " + videoPath;
        return result;
    }
    
    QByteArray bitstream = file.readAll();
    file.close();
    
    if (bitstream.isEmpty()) {
        result.errorMessage = "Empty file";
        return result;
    }
    
    // Simple NAL unit detection
    const uint8_t* data = reinterpret_cast<const uint8_t*>(bitstream.constData());
    int size = bitstream.size();
    int pos = 0;
    int nalCount = 0;
    
    while (pos < size - 4 && nalCount < 50) { // Limit for testing
        // Look for 0x00000001 start code
        if (data[pos] == 0x00 && data[pos+1] == 0x00 && data[pos+2] == 0x00 && data[pos+3] == 0x01) {
            int nalStart = pos + 4;
            
            // Find next start code
            int nalEnd = size;
            for (int searchPos = nalStart + 1; searchPos < size - 3; ++searchPos) {
                if ((data[searchPos] == 0x00 && data[searchPos+1] == 0x00 && data[searchPos+2] == 0x00 && data[searchPos+3] == 0x01) ||
                    (data[searchPos] == 0x00 && data[searchPos+1] == 0x00 && data[searchPos+2] == 0x01)) {
                    nalEnd = searchPos;
                    break;
                }
            }
            
            int nalSize = nalEnd - nalStart;
            if (nalSize > 0) {
                BasicNALInfo nalInfo;
                nalInfo.nalIndex = nalCount++;
                nalInfo.fileOffset = nalStart;
                nalInfo.size = nalSize;
                
                // Get NAL type from first byte
                if (nalStart < size) {
                    uint8_t nalHeader = data[nalStart];
                    nalInfo.nalType = nalHeader & 0x1F;
                }
                
                result.nalUnits.append(nalInfo);
            }
            pos = nalEnd;
        } else {
            pos++;
        }
    }
    
    result.success = true;
    return result;
}

void BitstreamAnalyzer::printAnalysis(const AnalysisResult &result) {
    if (!result.success) {
        qDebug() << "Analysis failed:" << result.errorMessage;
        return;
    }
    
    qDebug() << "=== H.264 Analysis Results ===";
    qDebug() << "NAL Units found:" << result.nalUnits.size();
    
    // Count different NAL types
    QMap<int, int> typeCounts;
    for (const auto &nal : result.nalUnits) {
        typeCounts[nal.nalType]++;
    }
    
    qDebug() << "NAL Type Distribution:";
    for (auto it = typeCounts.begin(); it != typeCounts.end(); ++it) {
        qDebug() << QString("  Type %1: %2 occurrences").arg(it.key()).arg(it.value());
    }
}

void BitstreamAnalyzer::saveAnalysisToFile(const AnalysisResult &result, const QString &videoPath) {
    QString outputPath = QFileInfo(videoPath).absolutePath() + "/" + 
                        QFileInfo(videoPath).baseName() + "_analysis.txt";
    
    QFile file(outputPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== H.264 Bitstream Analysis ===\n";
        out << "Analyzed: " << QDateTime::currentDateTime().toString() << "\n";
        out << "Video File: " << videoPath << "\n\n";
        out << "NAL Units Found: " << result.nalUnits.size() << "\n\n";
        
        // Type distribution
        QMap<int, int> typeCounts;
        for (const auto &nal : result.nalUnits) {
            typeCounts[nal.nalType]++;
        }
        
        out << "NAL Type Distribution:\n";
        for (auto it = typeCounts.begin(); it != typeCounts.end(); ++it) {
            QString typeName;
            switch(it.key()) {
                case 1: typeName = "Coded Slice (P/B-frame)"; break;
                case 5: typeName = "Coded Slice IDR (I-frame)"; break;
                case 7: typeName = "Sequence Parameter Set (SPS)"; break;
                case 8: typeName = "Picture Parameter Set (PPS)"; break;
                case 6: typeName = "Supplemental Enhancement Info (SEI)"; break;
                default: typeName = QString("Reserved/NAL Unit Type %1").arg(it.key()); break;
            }
            out << QString("  Type %1 (%2): %3 occurrences\n").arg(it.key()).arg(typeName).arg(it.value());
        }
        
        out << "\nDetailed NAL Unit List:\n";
        out << "Index\tType\tOffset\t\tSize(bytes)\n";
        out << "-----\t----\t------\t\t----------\n";
        
        for (const auto &nal : result.nalUnits) {
            out << QString("%1\t%2\t0x%3\t\t%4\n")
                   .arg(nal.nalIndex)
                   .arg(nal.nalType)
                   .arg(nal.fileOffset, 0, 16)
                   .arg(nal.size);
        }
        
        file.close();
        qDebug() << "Analysis saved to:" << outputPath;
    } else {
        qDebug() << "Failed to save analysis to:" << outputPath;
    }
}
