#ifndef DROPAREA_H
#define DROPAREA_H

#include <QFrame>
#include <QStringList>

class QLabel;
class QPushButton;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QVBoxLayout;

class DropArea final : public QFrame
{
    Q_OBJECT

public:
    explicit DropArea(QWidget *parent = nullptr);
    void setCompact(bool compact);
    void setExtractMode(bool extracting);
    void setInteractionEnabled(bool enabled);

signals:
    void filesDropped(const QStringList &paths);
    void chooseRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void setDragActive(bool active);

    QLabel *titleLabel_;
    QLabel *subtitleLabel_;
    QPushButton *chooseButton_;
    QVBoxLayout *layout_ = nullptr;
    bool compact_ = false;
};

#endif
