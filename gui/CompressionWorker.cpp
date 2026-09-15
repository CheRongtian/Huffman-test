#include "CompressionWorker.h"

#include "format.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>

#include <utility>

CompressionWorker::CompressionWorker(QVector<CompressionRequest> requests,
                                     QObject *parent)
    : QObject(parent), requests_(std::move(requests))
{
}

void CompressionWorker::requestCancel()
{
    cancelled_.store(true, std::memory_order_relaxed);
}

int CompressionWorker::progressCallback(uint64_t completedBytes,
                                        uint64_t totalBytes,
                                        void *userData)
{
    auto *context = static_cast<ProgressContext *>(userData);
    emit context->worker->taskProgress(
        context->taskIndex,
        static_cast<quint64>(completedBytes),
        static_cast<quint64>(totalBytes));

    return context->worker->cancelled_.load(std::memory_order_relaxed) ? 0 : 1;
}

void CompressionWorker::run()
{
    for (int index = 0; index < requests_.size(); index++)
    {
        if (cancelled_.load(std::memory_order_relaxed)) break;

        const CompressionRequest &request = requests_[index];
        emit taskStarted(index);

        ProgressContext context{this, index};
        CodecOptions options{&CompressionWorker::progressCallback, &context};
        CodecStats stats{};
        char error[256] = {0};
        QByteArray inputPath = QFile::encodeName(request.inputPath);
        QByteArray outputPath = QFile::encodeName(request.outputPath);
        QElapsedTimer timer;
        timer.start();

        int success = request.compress
            ? mgz_compress_file(inputPath.constData(), outputPath.constData(),
                                &options, &stats, error, sizeof(error))
            : mgz_decompress_file(inputPath.constData(), outputPath.constData(),
                                  &options, &stats, error, sizeof(error));

        emit taskFinished(
            index,
            success != 0,
            success ? QString() : QString::fromLocal8Bit(error),
            static_cast<quint64>(stats.input_size),
            static_cast<quint64>(stats.output_size),
            static_cast<quint32>(stats.compressed_blocks),
            static_cast<quint32>(stats.stored_blocks),
            timer.elapsed());

        if (cancelled_.load(std::memory_order_relaxed)) break;
    }

    emit allFinished(cancelled_.load(std::memory_order_relaxed));
}
