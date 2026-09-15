#include "DropArea.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

DropArea::DropArea(QWidget *parent)
    : QFrame(parent),
      titleLabel_(new QLabel(tr("Drop files here"), this)),
      subtitleLabel_(new QLabel(
          tr("Files stay on this computer and are processed locally."), this)),
      chooseButton_(new QPushButton(tr("Choose Files"), this))
{
    setObjectName(QStringLiteral("dropArea"));
    setProperty("dragActive", false);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(148);
    setAccessibleName(tr("File drop area"));
    setAccessibleDescription(
        tr("Drop files here or press Enter to open the file picker."));

    titleLabel_->setObjectName(QStringLiteral("dropTitle"));
    titleLabel_->setAlignment(Qt::AlignCenter);
    subtitleLabel_->setObjectName(QStringLiteral("secondaryText"));
    subtitleLabel_->setAlignment(Qt::AlignCenter);
    subtitleLabel_->setWordWrap(true);

    chooseButton_->setObjectName(QStringLiteral("secondaryButton"));
    chooseButton_->setMinimumHeight(40);
    chooseButton_->setCursor(Qt::PointingHandCursor);
    chooseButton_->setAccessibleName(tr("Choose files"));

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(24, 20, 24, 20);
    layout_->setSpacing(8);
    layout_->addStretch();
    layout_->addWidget(titleLabel_);
    layout_->addWidget(subtitleLabel_);
    layout_->addSpacing(4);
    layout_->addWidget(chooseButton_, 0, Qt::AlignHCenter);
    layout_->addStretch();

    connect(chooseButton_, &QPushButton::clicked,
            this, &DropArea::chooseRequested);
}

void DropArea::setCompact(bool compact)
{
    if (compact_ == compact) return;
    compact_ = compact;

    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    subtitleLabel_->setVisible(!compact);
    chooseButton_->setText(compact ? tr("Add Files") : tr("Choose Files"));
    layout_->setContentsMargins(24, compact ? 10 : 20,
                                24, compact ? 10 : 20);
    layout_->setSpacing(compact ? 4 : 8);
    layout_->invalidate();

    if (compact)
    {
        const int compactHeight = std::max(96, layout_->minimumSize().height());
        setFixedHeight(compactHeight);
    }
    else
    {
        setMinimumHeight(148);
    }
    updateGeometry();
}

void DropArea::setExtractMode(bool extracting)
{
    titleLabel_->setText(extracting
        ? tr("Drop MGZ archives here")
        : tr("Drop files here"));
    subtitleLabel_->setText(extracting
        ? tr("Archives are validated before their contents are restored.")
        : tr("Files stay on this computer and are processed locally."));
}

void DropArea::setInteractionEnabled(bool enabled)
{
    setAcceptDrops(enabled);
    setEnabled(enabled);
    chooseButton_->setEnabled(enabled);
    setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (!enabled) setDragActive(false);
}

void DropArea::setDragActive(bool active)
{
    if (property("dragActive").toBool() == active) return;
    setProperty("dragActive", active);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DropArea::dragEnterEvent(QDragEnterEvent *event)
{
    if (!isEnabled() || !event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }

    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
        {
            setDragActive(true);
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void DropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDragActive(false);
    event->accept();
}

void DropArea::dropEvent(QDropEvent *event)
{
    setDragActive(false);
    QStringList paths;

    for (const QUrl &url : event->mimeData()->urls())
    {
        if (!url.isLocalFile()) continue;
        QFileInfo info(url.toLocalFile());
        if (info.isFile()) paths.append(info.absoluteFilePath());
    }

    if (paths.isEmpty())
    {
        event->ignore();
        return;
    }

    emit filesDropped(paths);
    event->acceptProposedAction();
}

void DropArea::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space)
    {
        emit chooseRequested();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void DropArea::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit chooseRequested();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}
