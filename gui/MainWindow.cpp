#include "MainWindow.h"

#include "CompressionWorker.h"
#include "DropArea.h"
#include "LightningOverlay.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupMenus();
    applyTheme();
    refreshSummary();
}

MainWindow::~MainWindow()
{
    if (worker_ != nullptr) worker_->requestCancel();
    if (workerThread_ != nullptr && workerThread_->isRunning())
    {
        workerThread_->quit();
        workerThread_->wait();
    }
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("MGZ Compressor"));
    resize(920, 760);
    setMinimumSize(780, 700);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(16);
    root->setSizeConstraint(QLayout::SetMinimumSize);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(16);

    auto *titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(2);
    auto *title = new QLabel(tr("MGZ Compressor"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    auto *subtitle = new QLabel(
        tr("Local block compression with LZ77 and Canonical Huffman"), this);
    subtitle->setObjectName(QStringLiteral("secondaryText"));
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    auto *modeFrame = new QFrame(this);
    modeFrame->setObjectName(QStringLiteral("modeSelector"));
    auto *modeLayout = new QHBoxLayout(modeFrame);
    modeLayout->setContentsMargins(4, 4, 4, 4);
    modeLayout->setSpacing(2);

    compressModeButton_ = new QPushButton(tr("Compress"), modeFrame);
    extractModeButton_ = new QPushButton(tr("Extract"), modeFrame);
    for (QPushButton *button : {compressModeButton_, extractModeButton_})
    {
        button->setObjectName(QStringLiteral("modeButton"));
        button->setCheckable(true);
        button->setMinimumSize(96, 38);
        button->setCursor(Qt::PointingHandCursor);
    }
    compressModeButton_->setChecked(true);

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(compressModeButton_);
    modeGroup->addButton(extractModeButton_);
    modeLayout->addWidget(compressModeButton_);
    modeLayout->addWidget(extractModeButton_);
    headerLayout->addWidget(modeFrame);
    root->addLayout(headerLayout);

    dropArea_ = new DropArea(this);
    root->addWidget(dropArea_);

    auto *filesHeader = new QHBoxLayout;
    filesTitleLabel_ = new QLabel(tr("Files (0)"), this);
    filesTitleLabel_->setObjectName(QStringLiteral("sectionTitle"));
    clearButton_ = new QPushButton(tr("Clear"), this);
    clearButton_->setObjectName(QStringLiteral("quietButton"));
    clearButton_->setCursor(Qt::PointingHandCursor);
    clearButton_->setEnabled(false);
    filesHeader->addWidget(filesTitleLabel_);
    filesHeader->addStretch();
    filesHeader->addWidget(clearButton_);
    root->addLayout(filesHeader);

    fileTree_ = new QTreeWidget(this);
    fileTree_->setColumnCount(5);
    fileTree_->setHeaderLabels(
        {tr("File"), tr("Size"), tr("Output"), tr("Status"), QString()});
    fileTree_->setRootIsDecorated(false);
    fileTree_->setUniformRowHeights(false);
    fileTree_->setAlternatingRowColors(false);
    fileTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTree_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    fileTree_->setMinimumHeight(150);
    fileTree_->header()->setStretchLastSection(false);
    fileTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    fileTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    fileTree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    fileTree_->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    fileTree_->header()->setSectionResizeMode(4, QHeaderView::Fixed);
    fileTree_->setColumnWidth(3, 132);
    fileTree_->setColumnWidth(4, 42);
    fileTree_->setVisible(false);
    fileTree_->setAccessibleName(tr("Files to process"));
    root->addWidget(fileTree_, 1);

    auto *outputCard = new QFrame(this);
    outputCard->setObjectName(QStringLiteral("card"));
    outputCard->setMinimumHeight(140);
    auto *outputLayout = new QVBoxLayout(outputCard);
    outputLayout->setContentsMargins(16, 14, 16, 14);
    outputLayout->setSpacing(10);

    auto *outputTitle = new QLabel(tr("Output"), outputCard);
    outputTitle->setObjectName(QStringLiteral("sectionTitle"));
    outputLayout->addWidget(outputTitle);

    auto *directoryLayout = new QHBoxLayout;
    directoryLayout->setSpacing(8);
    locationCombo_ = new QComboBox(outputCard);
    locationCombo_->addItem(tr("Same folder as source"));
    locationCombo_->addItem(tr("Selected folder"));
    locationCombo_->setMinimumWidth(190);
    locationCombo_->setAccessibleName(tr("Output location mode"));
    outputDirectoryEdit_ = new QLineEdit(outputCard);
    outputDirectoryEdit_->setReadOnly(true);
    outputDirectoryEdit_->setPlaceholderText(tr("Choose an output folder"));
    outputDirectoryEdit_->setText(tr("Each source folder"));
    outputDirectoryEdit_->setAccessibleName(tr("Selected output directory"));
    browseOutputButton_ = new QPushButton(tr("Browse"), outputCard);
    browseOutputButton_->setObjectName(QStringLiteral("secondaryButton"));
    browseOutputButton_->setCursor(Qt::PointingHandCursor);
    browseOutputButton_->setEnabled(false);
    directoryLayout->addWidget(locationCombo_);
    directoryLayout->addWidget(outputDirectoryEdit_, 1);
    directoryLayout->addWidget(browseOutputButton_);
    outputLayout->addLayout(directoryLayout);

    auto *suffixLayout = new QHBoxLayout;
    suffixLayout->setSpacing(8);
    suffixLabel_ = new QLabel(tr("Archive suffix"), outputCard);
    suffixEdit_ = new QLineEdit(QStringLiteral(".mgz"), outputCard);
    suffixEdit_->setMaximumWidth(128);
    suffixEdit_->setMaxLength(16);
    suffixEdit_->setAccessibleName(tr("Archive suffix"));
    outputPreviewLabel_ = new QLabel(outputCard);
    outputPreviewLabel_->setObjectName(QStringLiteral("secondaryText"));
    outputPreviewLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    suffixLayout->addWidget(suffixLabel_);
    suffixLayout->addWidget(suffixEdit_);
    suffixLayout->addSpacing(8);
    suffixLayout->addWidget(outputPreviewLabel_, 1);
    outputLayout->addLayout(suffixLayout);
    root->addWidget(outputCard);

    bannerLabel_ = new QLabel(this);
    bannerLabel_->setObjectName(QStringLiteral("banner"));
    bannerLabel_->setWordWrap(true);
    bannerLabel_->setVisible(false);
    root->addWidget(bannerLabel_);

    resultCard_ = new QFrame(this);
    resultCard_->setObjectName(QStringLiteral("resultCard"));
    resultCard_->setProperty("resultState", QStringLiteral("success"));
    auto *resultLayout = new QHBoxLayout(resultCard_);
    resultLayout->setContentsMargins(16, 14, 16, 14);
    resultLayout->setSpacing(16);
    auto *resultTextLayout = new QVBoxLayout;
    resultTextLayout->setSpacing(4);
    resultTitleLabel_ = new QLabel(resultCard_);
    resultTitleLabel_->setObjectName(QStringLiteral("sectionTitle"));
    resultDetailsLabel_ = new QLabel(resultCard_);
    resultDetailsLabel_->setObjectName(QStringLiteral("secondaryText"));
    resultDetailsLabel_->setWordWrap(true);
    resultTextLayout->addWidget(resultTitleLabel_);
    resultTextLayout->addWidget(resultDetailsLabel_);
    showFolderButton_ = new QPushButton(tr("Show in Folder"), resultCard_);
    showFolderButton_->setObjectName(QStringLiteral("secondaryButton"));
    showFolderButton_->setCursor(Qt::PointingHandCursor);
    resultLayout->addLayout(resultTextLayout, 1);
    resultLayout->addWidget(showFolderButton_);
    resultCard_->setVisible(false);
    root->addWidget(resultCard_);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(10);
    footerInfoLabel_ = new QLabel(this);
    footerInfoLabel_->setObjectName(QStringLiteral("secondaryText"));
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setObjectName(QStringLiteral("secondaryButton"));
    cancelButton_->setMinimumHeight(44);
    cancelButton_->setCursor(Qt::PointingHandCursor);
    cancelButton_->setVisible(false);
    startButton_ = new QPushButton(tr("Compress Files"), this);
    startButton_->setObjectName(QStringLiteral("primaryButton"));
    startButton_->setMinimumSize(166, 44);
    startButton_->setCursor(Qt::PointingHandCursor);
    startButton_->setEnabled(false);
    footer->addWidget(footerInfoLabel_);
    footer->addStretch();
    footer->addWidget(cancelButton_);
    footer->addWidget(startButton_);
    root->addLayout(footer);

    connect(compressModeButton_, &QPushButton::clicked,
            this, [this] { setMode(true); });
    connect(extractModeButton_, &QPushButton::clicked,
            this, [this] { setMode(false); });
    connect(dropArea_, &DropArea::chooseRequested,
            this, &MainWindow::chooseFiles);
    connect(dropArea_, &DropArea::filesDropped,
            this, &MainWindow::addFiles);
    connect(clearButton_, &QPushButton::clicked,
            this, &MainWindow::clearTasks);
    connect(fileTree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current) {
                if (current == nullptr) return;
                for (const auto &task : tasks_)
                {
                    if (task->item == current && !task->error.isEmpty())
                    {
                        showBanner(tr("%1: %2")
                            .arg(QFileInfo(task->inputPath).fileName(),
                                 task->error), true);
                        return;
                    }
                }
            });
    connect(locationCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                browseOutputButton_->setEnabled(index == 1 && !processing_);
                outputDirectoryEdit_->setText(index == 0
                    ? tr("Each source folder")
                    : selectedOutputDirectory_);
                updateOutputPaths();
            });
    connect(browseOutputButton_, &QPushButton::clicked,
            this, &MainWindow::chooseOutputDirectory);
    connect(suffixEdit_, &QLineEdit::textChanged,
            this, [this] { updateOutputPaths(); });
    connect(suffixEdit_, &QLineEdit::editingFinished, this, [this] {
        suffixEdit_->setText(normalizedSuffix());
    });
    connect(startButton_, &QPushButton::clicked,
            this, &MainWindow::startProcessing);
    connect(cancelButton_, &QPushButton::clicked,
            this, &MainWindow::requestCancel);
    connect(showFolderButton_, &QPushButton::clicked,
            this, &MainWindow::showOutputFolder);

    lightningOverlay_ = new LightningOverlay(central);
}

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *addFilesAction = fileMenu->addAction(tr("&Choose Files..."));
    addFilesAction->setShortcut(QKeySequence::Open);
    connect(addFilesAction, &QAction::triggered, this, &MainWindow::chooseFiles);

    QAction *outputAction = fileMenu->addAction(tr("Choose &Output Folder..."));
    connect(outputAction, &QAction::triggered,
            this, &MainWindow::chooseOutputDirectory);

    QAction *clearAction = fileMenu->addAction(tr("&Clear File List"));
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearTasks);

    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("&About MGZ Compressor"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            tr("About MGZ Compressor"),
            tr("MGZ Compressor 1.1\n\n"
               "A local block-compression utility built with Qt, LZ77, "
               "Canonical Huffman coding, and CRC32."));
    });
}

void MainWindow::applyTheme()
{
    const QPalette current = QApplication::palette();
    const bool dark = current.color(QPalette::Window).lightness() < 128;
    const QString background = dark ? QStringLiteral("#151515")
                                    : QStringLiteral("#FFFFFF");
    const QString surface = dark ? QStringLiteral("#1E1E1E")
                                 : QStringLiteral("#F7F7F6");
    const QString foreground = dark ? QStringLiteral("#F5F5F5")
                                    : QStringLiteral("#171717");
    const QString secondary = dark ? QStringLiteral("#B8B8B8")
                                   : QStringLiteral("#475569");
    const QString border = dark ? QStringLiteral("#3D3D3D")
                                : QStringLiteral("#E0E0DE");
    const QString muted = dark ? QStringLiteral("#2A2A2A")
                               : QStringLiteral("#E8ECF0");
    const QString accent = dark ? QStringLiteral("#D4A84B")
                                : QStringLiteral("#A16207");
    const QString onAccent = dark ? QStringLiteral("#171717")
                                  : QStringLiteral("#FFFFFF");
    const QString error = dark ? QStringLiteral("#F87171")
                               : QStringLiteral("#B91C1C");
    const QString success = dark ? QStringLiteral("#4ADE80")
                                 : QStringLiteral("#15803D");
    const QString accentHover = dark ? QStringLiteral("#E4BB62")
                                     : QStringLiteral("#854D0E");
    const QString accentPressed = dark ? QStringLiteral("#C8952D")
                                       : QStringLiteral("#713F12");

    QString sheet = QStringLiteral(R"(
        QWidget#centralWidget {
            background: %1;
            color: %3;
        }
        QLabel#appTitle {
            color: %3;
            font-size: 24px;
            font-weight: 700;
        }
        QLabel#sectionTitle, QLabel#dropTitle {
            color: %3;
            font-size: 15px;
            font-weight: 650;
        }
        QLabel#secondaryText {
            color: %4;
        }
        QFrame#modeSelector {
            background: %2;
            border: 1px solid %5;
            border-radius: 10px;
        }
        QPushButton#modeButton {
            min-height: 30px;
            padding: 0 14px;
            color: %4;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 7px;
        }
        QPushButton#modeButton:checked {
            color: %3;
            background: %1;
            border-color: %5;
        }
        QFrame#dropArea {
            background: %2;
            border: 2px dashed %5;
            border-radius: 12px;
        }
        QFrame#dropArea:hover, QFrame#dropArea:focus,
        QFrame#dropArea[dragActive="true"] {
            border-color: %7;
        }
        QFrame#card, QFrame#resultCard {
            background: %2;
            border: 1px solid %5;
            border-radius: 10px;
        }
        QFrame#resultCard[resultState="success"] {
            border-color: %10;
        }
        QFrame#resultCard[resultState="error"] {
            border-color: %9;
        }
        QPushButton {
            padding: 7px 14px;
            border: 1px solid %5;
            border-radius: 7px;
            background: %2;
            color: %3;
        }
        QPushButton:hover {
            border-color: %7;
        }
        QPushButton:focus {
            border: 2px solid %7;
        }
        QPushButton:disabled {
            color: %4;
            background: %6;
        }
        QPushButton#primaryButton {
            color: %8;
            background: %7;
            border-color: %7;
            font-weight: 650;
        }
        QPushButton#primaryButton:hover {
            background: %11;
            border-color: %11;
        }
        QPushButton#primaryButton:pressed {
            background: %12;
            border-color: %12;
        }
        QPushButton#quietButton {
            background: transparent;
            border-color: transparent;
        }
        QLineEdit, QComboBox {
            min-height: 34px;
            padding: 0 9px;
            color: %3;
            background: %1;
            border: 1px solid %5;
            border-radius: 7px;
        }
        QLineEdit[readOnly="true"] {
            background: %6;
            color: %4;
        }
        QLineEdit:focus, QComboBox:focus {
            border: 2px solid %7;
        }
        QTreeWidget {
            color: %3;
            background: %1;
            alternate-background-color: %2;
            border: 1px solid %5;
            border-radius: 10px;
            outline: 0;
        }
        QTreeWidget::item {
            min-height: 42px;
            border-bottom: 1px solid %5;
        }
        QTreeWidget::item:selected {
            color: %3;
            background: %6;
        }
        QHeaderView::section {
            color: %4;
            background: %2;
            padding: 8px;
            border: 0;
            border-bottom: 1px solid %5;
            font-weight: 600;
        }
        QProgressBar {
            min-height: 24px;
            color: %3;
            background: %6;
            border: 0;
            border-radius: 6px;
            text-align: center;
        }
        QProgressBar::chunk {
            background: %7;
            border-radius: 6px;
        }
        QToolButton {
            min-width: 30px;
            min-height: 30px;
            border: 1px solid transparent;
            border-radius: 6px;
        }
        QToolButton:hover, QToolButton:focus {
            background: %6;
            border-color: %5;
        }
        QLabel#banner {
            padding: 10px 12px;
            border: 1px solid %5;
            border-radius: 7px;
            background: %2;
            color: %3;
        }
        QLabel#banner[bannerType="error"] {
            border-color: %9;
            color: %9;
        }
    )");

    sheet = sheet.arg(background)
                 .arg(surface)
                 .arg(foreground)
                 .arg(secondary)
                 .arg(border)
                 .arg(muted)
                 .arg(accent)
                 .arg(onAccent)
                 .arg(error)
                 .arg(success)
                 .arg(accentHover)
                 .arg(accentPressed);
    setStyleSheet(sheet);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange)
    {
        applyTheme();
    }
}

void MainWindow::setMode(bool compressing)
{
    if (compressing == compressMode_) return;

    if (!tasks_.empty())
    {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Change mode?"),
            tr("Changing mode will clear the current file list."),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
        {
            QSignalBlocker compressBlocker(compressModeButton_);
            QSignalBlocker extractBlocker(extractModeButton_);
            compressModeButton_->setChecked(compressMode_);
            extractModeButton_->setChecked(!compressMode_);
            return;
        }
        clearTasks();
    }

    compressMode_ = compressing;
    dropArea_->setExtractMode(!compressMode_);
    suffixLabel_->setText(compressMode_
        ? tr("Archive suffix")
        : tr("Suffix to remove"));
    suffixEdit_->setAccessibleName(suffixLabel_->text());
    startButton_->setText(compressMode_ ? tr("Compress Files")
                                        : tr("Extract Files"));
    hideBanner();
    refreshSummary();
}

void MainWindow::chooseFiles()
{
    if (processing_) return;

    const QString filter = compressMode_
        ? tr("All files (*)")
        : tr("MGZ archives (*.mgz);;All files (*)");
    QStringList paths = QFileDialog::getOpenFileNames(
        this,
        compressMode_ ? tr("Choose files to compress")
                      : tr("Choose archives to extract"),
        QString(),
        filter);
    if (!paths.isEmpty()) addFiles(paths);
}

bool MainWindow::isMgzArchive(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return file.read(4) == QByteArrayLiteral("MGZ1");
}

void MainWindow::addFiles(const QStringList &paths)
{
    if (processing_) return;

    QSet<QString> existing;
    for (const auto &task : tasks_)
    {
        existing.insert(QDir::cleanPath(task->inputPath).toCaseFolded());
    }

    QStringList rejected;
    int added = 0;
    QFileIconProvider iconProvider;

    for (const QString &path : paths)
    {
        QFileInfo info(path);
        QString absolutePath = info.absoluteFilePath();
        QString key = QDir::cleanPath(absolutePath).toCaseFolded();

        if (!info.exists() || !info.isFile() || existing.contains(key)) continue;
        if (!compressMode_ && !isMgzArchive(absolutePath))
        {
            rejected.append(info.fileName());
            continue;
        }

        auto task = std::make_unique<TaskEntry>();
        task->inputPath = absolutePath;
        task->inputSize = static_cast<quint64>(info.size());
        task->item = new QTreeWidgetItem(fileTree_);
        task->item->setIcon(0, iconProvider.icon(info));
        task->item->setText(0, info.fileName());
        task->item->setToolTip(0, absolutePath);
        task->item->setText(1, formatBytes(task->inputSize));
        task->item->setSizeHint(0, QSize(0, 46));

        task->progress = new QProgressBar(fileTree_);
        task->progress->setRange(0, 100);
        task->progress->setValue(0);
        task->progress->setFormat(tr("Ready"));
        task->progress->setAccessibleName(
            tr("Status for %1").arg(info.fileName()));
        fileTree_->setItemWidget(task->item, 3, task->progress);

        task->removeButton = new QToolButton(fileTree_);
        task->removeButton->setIcon(style()->standardIcon(
            QStyle::SP_DialogCloseButton));
        task->removeButton->setToolTip(tr("Remove %1").arg(info.fileName()));
        task->removeButton->setAccessibleName(
            tr("Remove %1 from the file list").arg(info.fileName()));
        task->removeButton->setCursor(Qt::PointingHandCursor);
        fileTree_->setItemWidget(task->item, 4, task->removeButton);

        TaskEntry *taskPointer = task.get();
        connect(task->removeButton, &QToolButton::clicked, this,
                [this, taskPointer] {
                    QTimer::singleShot(0, this,
                        [this, taskPointer] { removeTask(taskPointer); });
                });

        tasks_.push_back(std::move(task));
        existing.insert(key);
        added++;
    }

    if (added > 0)
    {
        resultCard_->setVisible(false);
        hideBanner();
    }
    if (!rejected.isEmpty())
    {
        QString names = rejected.mid(0, 3).join(QStringLiteral(", "));
        if (rejected.size() > 3) names += tr(" and %1 more").arg(rejected.size() - 3);
        showBanner(tr("Skipped files without a valid MGZ header: %1").arg(names),
                   true);
    }

    updateOutputPaths();
}

void MainWindow::removeTask(TaskEntry *task)
{
    if (processing_ || task == nullptr) return;

    auto position = std::find_if(tasks_.begin(), tasks_.end(),
        [task](const std::unique_ptr<TaskEntry> &candidate) {
            return candidate.get() == task;
        });
    if (position == tasks_.end()) return;

    delete (*position)->item;
    tasks_.erase(position);
    resultCard_->setVisible(false);
    updateOutputPaths();
}

void MainWindow::clearTasks()
{
    if (processing_) return;
    fileTree_->clear();
    tasks_.clear();
    activeTasks_.clear();
    resultCard_->setVisible(false);
    hideBanner();
    refreshSummary();
}

void MainWindow::chooseOutputDirectory()
{
    if (processing_) return;

    QString initial = selectedOutputDirectory_.isEmpty()
        ? QDir::homePath()
        : selectedOutputDirectory_;
    QString directory = QFileDialog::getExistingDirectory(
        this, tr("Choose output folder"), initial,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) return;

    selectedOutputDirectory_ = QDir::cleanPath(directory);
    if (locationCombo_->currentIndex() != 1) locationCombo_->setCurrentIndex(1);
    outputDirectoryEdit_->setText(selectedOutputDirectory_);
    updateOutputPaths();
}

QString MainWindow::normalizedSuffix() const
{
    QString suffix = suffixEdit_->text().trimmed();
    if (suffix.isEmpty()) suffix = QStringLiteral(".mgz");
    if (!suffix.startsWith(QLatin1Char('.'))) suffix.prepend(QLatin1Char('.'));
    return suffix;
}

QString MainWindow::proposedOutputPath(const TaskEntry &task) const
{
    QFileInfo inputInfo(task.inputPath);
    QString outputDirectory = locationCombo_->currentIndex() == 0
        ? inputInfo.absolutePath()
        : selectedOutputDirectory_;
    QString suffix = normalizedSuffix();
    QString outputName;

    if (compressMode_)
    {
        outputName = inputInfo.fileName() + suffix;
    }
    else
    {
        outputName = inputInfo.fileName();
        if (outputName.endsWith(suffix, Qt::CaseInsensitive) &&
            outputName.size() > suffix.size())
        {
            outputName.chop(suffix.size());
        }
        else
        {
            outputName += QStringLiteral(".restored");
        }
    }

    QString outputPath = QDir(outputDirectory).filePath(outputName);
    if (QDir::cleanPath(outputPath).compare(
            QDir::cleanPath(task.inputPath), Qt::CaseInsensitive) == 0)
    {
        outputPath += QStringLiteral(".restored");
    }
    return QDir::cleanPath(outputPath);
}

QString MainWindow::makeNumberedPath(const QString &path, int number) const
{
    QFileInfo info(path);
    QString fileName = info.fileName();
    int dot = fileName.lastIndexOf(QLatin1Char('.'));
    QString stem = dot > 0 ? fileName.left(dot) : fileName;
    QString extension = dot > 0 ? fileName.mid(dot) : QString();
    QString numbered = QStringLiteral("%1 (%2)%3")
        .arg(stem).arg(number).arg(extension);
    return QDir(info.absolutePath()).filePath(numbered);
}

void MainWindow::updateOutputPaths()
{
    QSet<QString> usedPaths;

    for (const auto &task : tasks_)
    {
        QString proposed = proposedOutputPath(*task);
        QString unique = proposed;
        int number = 1;

        while (usedPaths.contains(QDir::cleanPath(unique).toCaseFolded()))
        {
            unique = makeNumberedPath(proposed, number++);
        }
        usedPaths.insert(QDir::cleanPath(unique).toCaseFolded());
        task->outputPath = unique;
        task->item->setText(2, QFileInfo(unique).fileName());
        task->item->setToolTip(2, unique);
    }

    refreshSummary();
}

QString MainWindow::formatBytes(quint64 bytes) const
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        unit++;
    }
    int decimals = unit == 0 ? 0 : (value >= 100.0 ? 0 : 1);
    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', decimals),
             QString::fromLatin1(units[unit]));
}

void MainWindow::refreshSummary()
{
    quint64 totalSize = 0;
    for (const auto &task : tasks_) totalSize += task->inputSize;

    const qulonglong taskCount = static_cast<qulonglong>(tasks_.size());
    dropArea_->setCompact(!tasks_.empty());
    filesTitleLabel_->setText(tr("Files (%1)").arg(taskCount));
    fileTree_->setVisible(!tasks_.empty());
    clearButton_->setEnabled(!tasks_.empty() && !processing_);

    if (tasks_.empty())
    {
        footerInfoLabel_->setText(tr("No files selected"));
        outputPreviewLabel_->setText(tr("Add files to preview output names"));
    }
    else
    {
        footerInfoLabel_->setText(tr("%1 file(s) · %2")
            .arg(taskCount).arg(formatBytes(totalSize)));
        QString firstOutput = QFileInfo(tasks_.front()->outputPath).fileName();
        outputPreviewLabel_->setText(tasks_.size() == 1
            ? tr("Output: %1").arg(firstOutput)
            : tr("Example: %1 · %2 outputs")
                  .arg(firstOutput).arg(taskCount));
    }

    const bool outputReady = locationCombo_->currentIndex() == 0 ||
                             !selectedOutputDirectory_.isEmpty();
    startButton_->setEnabled(!processing_ && !tasks_.empty() && outputReady);
    startButton_->setText(compressMode_
        ? tr("Compress %1 File(s)").arg(taskCount)
        : tr("Extract %1 File(s)").arg(taskCount));
}

bool MainWindow::resolveExistingOutputs()
{
    QSet<QString> inputPaths;
    QSet<QString> usedPaths;
    for (const auto &task : tasks_)
    {
        inputPaths.insert(QDir::cleanPath(task->inputPath).toCaseFolded());
    }

    for (const auto &task : tasks_)
    {
        QString original = task->outputPath;
        QString candidate = original;
        int number = 1;
        QString key = QDir::cleanPath(candidate).toCaseFolded();
        while (inputPaths.contains(key) || usedPaths.contains(key))
        {
            candidate = makeNumberedPath(original, number++);
            key = QDir::cleanPath(candidate).toCaseFolded();
        }
        task->outputPath = candidate;
        task->item->setText(2, QFileInfo(candidate).fileName());
        task->item->setToolTip(2, candidate);
        usedPaths.insert(key);
    }

    bool hasExisting = std::any_of(tasks_.begin(), tasks_.end(),
        [](const std::unique_ptr<TaskEntry> &task) {
            return QFileInfo::exists(task->outputPath);
        });
    if (!hasExisting) return true;

    QMessageBox box(QMessageBox::Warning,
                    tr("Output files already exist"),
                    tr("One or more output files already exist. Choose how "
                       "to continue."),
                    QMessageBox::NoButton,
                    this);
    QAbstractButton *replaceButton = box.addButton(
        tr("Replace Existing"), QMessageBox::AcceptRole);
    QAbstractButton *renameButton = box.addButton(
        tr("Create New Names"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(qobject_cast<QPushButton *>(renameButton));
    box.exec();

    if (box.clickedButton() == replaceButton) return true;
    if (box.clickedButton() != renameButton) return false;

    usedPaths.clear();
    for (const auto &task : tasks_)
    {
        QString original = task->outputPath;
        QString candidate = original;
        int number = 1;
        QString key = QDir::cleanPath(candidate).toCaseFolded();
        while (QFileInfo::exists(candidate) || usedPaths.contains(key) ||
               inputPaths.contains(key))
        {
            candidate = makeNumberedPath(original, number++);
            key = QDir::cleanPath(candidate).toCaseFolded();
        }
        task->outputPath = candidate;
        task->item->setText(2, QFileInfo(candidate).fileName());
        task->item->setToolTip(2, candidate);
        usedPaths.insert(key);
    }
    return true;
}

void MainWindow::startProcessing()
{
    if (processing_ || tasks_.empty()) return;

    const QString suffix = normalizedSuffix();
    static const QRegularExpression validSuffix(
        QStringLiteral(R"(^\.[A-Za-z0-9][A-Za-z0-9._-]{0,14}$)"));
    if (!validSuffix.match(suffix).hasMatch())
    {
        showBanner(tr("Use a suffix such as .mgz, .huf, or .archive-1."), true);
        suffixEdit_->setFocus();
        return;
    }
    suffixEdit_->setText(suffix);

    if (locationCombo_->currentIndex() == 1 &&
        (selectedOutputDirectory_.isEmpty() ||
         !QFileInfo(selectedOutputDirectory_).isDir()))
    {
        showBanner(tr("Choose a valid output folder before continuing."), true);
        return;
    }

    updateOutputPaths();
    if (!resolveExistingOutputs()) return;

    QVector<CompressionRequest> requests;
    activeTasks_.clear();
    requests.reserve(static_cast<qsizetype>(tasks_.size()));
    activeTasks_.reserve(static_cast<qsizetype>(tasks_.size()));

    for (const auto &task : tasks_)
    {
        task->state = TaskState::Ready;
        task->error.clear();
        task->progress->setRange(0, 100);
        task->progress->setValue(0);
        task->progress->setFormat(tr("Waiting"));
        requests.append({task->inputPath, task->outputPath, compressMode_});
        activeTasks_.append(task.get());
    }

    resultInputSize_ = 0;
    resultOutputSize_ = 0;
    resultCompressedBlocks_ = 0;
    resultStoredBlocks_ = 0;
    resultElapsedMilliseconds_ = 0;
    resultSuccessCount_ = 0;
    resultFailureCount_ = 0;
    resultCard_->setVisible(false);
    hideBanner();
    setProcessing(true);

    auto *thread = new QThread(this);
    auto *worker = new CompressionWorker(std::move(requests));
    worker->moveToThread(thread);
    workerThread_ = thread;
    worker_ = worker;

    connect(thread, &QThread::started, worker, &CompressionWorker::run);
    connect(worker, &CompressionWorker::taskStarted, this,
            [this](int index) {
                if (index < 0 || index >= activeTasks_.size()) return;
                TaskEntry *task = activeTasks_[index];
                task->state = TaskState::Processing;
                task->progress->setRange(0, 0);
                task->progress->setFormat(compressMode_
                    ? tr("Compressing") : tr("Extracting"));
            });
    connect(worker, &CompressionWorker::taskProgress, this,
            [this](int index, quint64 completed, quint64 total) {
                if (index < 0 || index >= activeTasks_.size()) return;
                QProgressBar *progress = activeTasks_[index]->progress;
                if (total == 0)
                {
                    progress->setRange(0, 0);
                    return;
                }
                int percent = static_cast<int>(std::min<quint64>(
                    100, static_cast<quint64>(std::llround(
                        static_cast<double>(completed) * 100.0 /
                        static_cast<double>(total)))));
                progress->setRange(0, 100);
                progress->setValue(percent);
                progress->setFormat(tr("%1%").arg(percent));
            });
    connect(worker, &CompressionWorker::taskFinished, this,
            [this](int index, bool success, const QString &error,
                   quint64 inputSize, quint64 outputSize,
                   quint32 compressedBlocks, quint32 storedBlocks,
                   qint64 elapsedMilliseconds) {
                if (index < 0 || index >= activeTasks_.size()) return;
                TaskEntry *task = activeTasks_[index];
                task->progress->setRange(0, 100);

                if (success)
                {
                    task->state = TaskState::Completed;
                    task->error.clear();
                    task->progress->setAccessibleDescription(QString());
                    task->progress->setValue(100);
                    task->progress->setFormat(tr("Done"));
                    task->item->setToolTip(3, tr("Completed successfully"));
                    resultInputSize_ += inputSize;
                    resultOutputSize_ += outputSize;
                    resultCompressedBlocks_ += compressedBlocks;
                    resultStoredBlocks_ += storedBlocks;
                    resultElapsedMilliseconds_ += elapsedMilliseconds;
                    resultSuccessCount_++;
                }
                else
                {
                    const bool cancelled = error.contains(
                        QStringLiteral("cancel"), Qt::CaseInsensitive);
                    task->state = cancelled ? TaskState::Cancelled
                                            : TaskState::Failed;
                    task->error = error;
                    task->progress->setValue(0);
                    task->progress->setFormat(cancelled
                        ? tr("Cancelled") : tr("Failed"));
                    task->item->setToolTip(3, error);
                    task->progress->setAccessibleDescription(error);
                    resultFailureCount_++;
                    showBanner(tr("%1: %2")
                        .arg(QFileInfo(task->inputPath).fileName(), error), true);
                }
            });
    connect(worker, &CompressionWorker::allFinished, this,
            [this](bool cancelled) {
                for (TaskEntry *task : activeTasks_)
                {
                    if (task->state == TaskState::Ready)
                    {
                        task->state = TaskState::Cancelled;
                        task->progress->setRange(0, 100);
                        task->progress->setValue(0);
                        task->progress->setFormat(tr("Cancelled"));
                    }
                }
                setProcessing(false);
                showResult(cancelled);
            });
    connect(worker, &CompressionWorker::allFinished,
            thread, &QThread::quit);
    connect(thread, &QThread::finished,
            worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread] {
        if (workerThread_ == thread) workerThread_ = nullptr;
        worker_ = nullptr;
        thread->deleteLater();
        if (closeWhenFinished_)
        {
            closeWhenFinished_ = false;
            QTimer::singleShot(0, this, &QWidget::close);
        }
    });
    thread->start();
}

void MainWindow::setProcessing(bool processing)
{
    processing_ = processing;
    compressModeButton_->setEnabled(!processing);
    extractModeButton_->setEnabled(!processing);
    dropArea_->setInteractionEnabled(!processing);
    clearButton_->setEnabled(!processing && !tasks_.empty());
    locationCombo_->setEnabled(!processing);
    outputDirectoryEdit_->setEnabled(!processing);
    browseOutputButton_->setEnabled(!processing &&
                                    locationCombo_->currentIndex() == 1);
    suffixEdit_->setEnabled(!processing);
    startButton_->setEnabled(!processing && !tasks_.empty());
    cancelButton_->setVisible(processing);
    cancelButton_->setEnabled(processing);
    cancelButton_->setText(tr("Cancel"));

    for (const auto &task : tasks_)
    {
        task->removeButton->setEnabled(!processing);
    }

    if (!processing) refreshSummary();
}

void MainWindow::requestCancel()
{
    if (!processing_ || worker_ == nullptr) return;
    worker_->requestCancel();
    cancelButton_->setEnabled(false);
    cancelButton_->setText(tr("Cancelling..."));
    footerInfoLabel_->setText(tr("Finishing the current block safely..."));
}

void MainWindow::showBanner(const QString &message, bool error)
{
    bannerLabel_->setText(message);
    bannerLabel_->setProperty("bannerType",
                              error ? QStringLiteral("error")
                                    : QStringLiteral("info"));
    bannerLabel_->style()->unpolish(bannerLabel_);
    bannerLabel_->style()->polish(bannerLabel_);
    bannerLabel_->setVisible(true);
}

void MainWindow::hideBanner()
{
    bannerLabel_->setVisible(false);
    bannerLabel_->clear();
}

void MainWindow::showResult(bool cancelled)
{
    const bool hasFailure = resultFailureCount_ > 0 || cancelled;
    resultCard_->setProperty("resultState", hasFailure
        ? QStringLiteral("error") : QStringLiteral("success"));
    resultCard_->style()->unpolish(resultCard_);
    resultCard_->style()->polish(resultCard_);

    if (cancelled)
    {
        resultTitleLabel_->setText(tr("Processing cancelled"));
    }
    else if (resultFailureCount_ > 0)
    {
        resultTitleLabel_->setText(tr("Completed with errors"));
    }
    else
    {
        resultTitleLabel_->setText(compressMode_
            ? tr("Compression complete") : tr("Extraction complete"));
    }

    QString details = tr("%1 succeeded · %2 failed")
        .arg(resultSuccessCount_).arg(resultFailureCount_);
    if (resultSuccessCount_ > 0)
    {
        if (compressMode_)
        {
            double saving = resultInputSize_ == 0
                ? 0.0
                : (1.0 - static_cast<double>(resultOutputSize_) /
                             static_cast<double>(resultInputSize_)) * 100.0;
            details += tr(" · %1 → %2 · %3% saved")
                .arg(formatBytes(resultInputSize_),
                     formatBytes(resultOutputSize_),
                     QString::number(saving, 'f', 1));
        }
        else
        {
            details += tr(" · %1 restored").arg(formatBytes(resultOutputSize_));
        }
        details += tr(" · %1 compressed / %2 stored blocks · %3 s")
            .arg(resultCompressedBlocks_)
            .arg(resultStoredBlocks_)
            .arg(QString::number(resultElapsedMilliseconds_ / 1000.0, 'f', 2));
    }

    resultDetailsLabel_->setText(details);
    showFolderButton_->setVisible(resultSuccessCount_ > 0);
    resultCard_->setVisible(true);

    if (!hasFailure && resultSuccessCount_ > 0)
        lightningOverlay_->play();
}

void MainWindow::showOutputFolder()
{
    for (TaskEntry *task : activeTasks_)
    {
        if (task->state == TaskState::Completed)
        {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(task->outputPath).absolutePath()));
            return;
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!processing_)
    {
        event->accept();
        return;
    }

    QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Cancel processing and quit?"),
        tr("The current block will finish safely before the application closes."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer == QMessageBox::Yes)
    {
        closeWhenFinished_ = true;
        requestCancel();
    }
    event->ignore();
}
