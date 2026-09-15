#ifndef LIGHTNINGOVERLAY_H
#define LIGHTNINGOVERLAY_H

#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QEvent;
class QPaintEvent;

class LightningOverlay final : public QWidget
{
public:
    explicit LightningOverlay(QWidget *parent = nullptr);

    void play();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    using BoltPath = QVector<QPointF>;

    void rebuildBolt();
    void stop();

    QElapsedTimer elapsed_;
    QTimer frameTimer_;
    BoltPath mainBolt_;
    QVector<BoltPath> branches_;
    int durationMilliseconds_ = 420;
};

#endif
