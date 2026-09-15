#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

class CompressionWorker;
class DropArea;
class LightningOverlay;
class QCloseEvent;
class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QThread;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class TaskState
    {
        Ready,
        Processing,
        Completed,
        Failed,
        Cancelled
    };

    struct TaskEntry
    {
        QString inputPath;
        QString outputPath;
        QString error;
        quint64 inputSize = 0;
        TaskState state = TaskState::Ready;
        QTreeWidgetItem *item = nullptr;
        QProgressBar *progress = nullptr;
        QToolButton *removeButton = nullptr;
    };

    void setupUi();
    void setupMenus();
    void applyTheme();
    void setMode(bool compressing);
    void chooseFiles();
    void addFiles(const QStringList &paths);
    void removeTask(TaskEntry *task);
    void clearTasks();
    void chooseOutputDirectory();
    void updateOutputPaths();
    void refreshSummary();
    void startProcessing();
    void requestCancel();
    void setProcessing(bool processing);
    void showBanner(const QString &message, bool error);
    void hideBanner();
    void showResult(bool cancelled);
    void showOutputFolder();

    QString normalizedSuffix() const;
    QString proposedOutputPath(const TaskEntry &task) const;
    QString makeNumberedPath(const QString &path, int number) const;
    QString formatBytes(quint64 bytes) const;
    bool isMgzArchive(const QString &path) const;
    bool resolveExistingOutputs();

    bool compressMode_ = true;
    bool processing_ = false;
    bool closeWhenFinished_ = false;
    QString selectedOutputDirectory_;

    std::vector<std::unique_ptr<TaskEntry>> tasks_;
    QVector<TaskEntry *> activeTasks_;

    quint64 resultInputSize_ = 0;
    quint64 resultOutputSize_ = 0;
    quint32 resultCompressedBlocks_ = 0;
    quint32 resultStoredBlocks_ = 0;
    qint64 resultElapsedMilliseconds_ = 0;
    int resultSuccessCount_ = 0;
    int resultFailureCount_ = 0;

    DropArea *dropArea_ = nullptr;
    LightningOverlay *lightningOverlay_ = nullptr;
    QPushButton *compressModeButton_ = nullptr;
    QPushButton *extractModeButton_ = nullptr;
    QLabel *filesTitleLabel_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QTreeWidget *fileTree_ = nullptr;
    QComboBox *locationCombo_ = nullptr;
    QLineEdit *outputDirectoryEdit_ = nullptr;
    QPushButton *browseOutputButton_ = nullptr;
    QLabel *suffixLabel_ = nullptr;
    QLineEdit *suffixEdit_ = nullptr;
    QLabel *outputPreviewLabel_ = nullptr;
    QLabel *bannerLabel_ = nullptr;
    QFrame *resultCard_ = nullptr;
    QLabel *resultTitleLabel_ = nullptr;
    QLabel *resultDetailsLabel_ = nullptr;
    QPushButton *showFolderButton_ = nullptr;
    QLabel *footerInfoLabel_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QPushButton *startButton_ = nullptr;

    QPointer<QThread> workerThread_;
    QPointer<CompressionWorker> worker_;
};

#endif
