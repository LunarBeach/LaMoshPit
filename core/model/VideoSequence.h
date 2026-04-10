#pragma once
#include <QString>
#include <QDebug>

class VideoSequence {
public:
    VideoSequence() = default;
    ~VideoSequence() = default;

    bool load(const QString& filePath) {
        m_filePath = filePath;
        qDebug() << "VideoSequence initialized with deterministic file:" << m_filePath;
        
        // TODO: In the future, we will open the file here, 
        // parse the H.264 bitstream, and populate our HackedFrame lists.
        return true;
    }

    QString getFilePath() const { return m_filePath; }

private:
    QString m_filePath;
};