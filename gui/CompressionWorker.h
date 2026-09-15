#ifndef COMPRESSIONWORKER_H
#define COMPRESSIONWORKER_H

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <cstdint>

struct CompressionRequest
{
    QString inputPath;
    QString outputPath;
    bool compress;
};

class CompressionWorker final : public QObject
{
    Q_OBJECT

public:
    explicit CompressionWorker(QVector<CompressionRequest> requests,
                               QObject *parent = nullptr);
    void requestCancel();

public slots:
    void run();

signals:
    void taskStarted(int index);
    void taskProgress(int index, quint64 completedBytes, quint64 totalBytes);
    void taskFinished(int index, bool success, const QString &error,
                      quint64 inputSize, quint64 outputSize,
                      quint32 compressedBlocks, quint32 storedBlocks,
                      qint64 elapsedMilliseconds);
    void allFinished(bool cancelled);

private:
    struct ProgressContext
    {
        CompressionWorker *worker;
        int taskIndex;
    };

    static int progressCallback(uint64_t completedBytes,
                                uint64_t totalBytes,
                                void *userData);

    QVector<CompressionRequest> requests_;
    std::atomic_bool cancelled_{false};
};

#endif
