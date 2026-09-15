#include "LightningOverlay.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRandomGenerator>
#include <QStyle>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
qreal randomBetween(QRandomGenerator *generator, qreal minimum, qreal maximum)
{
    return minimum + (maximum - minimum) * generator->generateDouble();
}

void appendBolt(QPainterPath &path, const QVector<QPointF> &points)
{
    if (points.size() < 2) return;

    path.moveTo(points.front());
    for (qsizetype index = 1; index < points.size(); ++index)
        path.lineTo(points[index]);
}
}

LightningOverlay::LightningOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    hide();

    if (parent != nullptr)
    {
        setGeometry(parent->rect());
        parent->installEventFilter(this);
    }

    frameTimer_.setInterval(16);
    frameTimer_.setTimerType(Qt::PreciseTimer);
    connect(&frameTimer_, &QTimer::timeout, this, [this] {
        if (!elapsed_.isValid() ||
            elapsed_.elapsed() >= durationMilliseconds_)
        {
            stop();
            return;
        }
        update();
    });
}

void LightningOverlay::play()
{
    const int styleDuration = style()->styleHint(
        QStyle::SH_Widget_Animation_Duration, nullptr, this);
    if (styleDuration <= 0)
    {
        stop();
        return;
    }

    durationMilliseconds_ = std::clamp(styleDuration * 2, 360, 520);
    if (parentWidget() != nullptr) setGeometry(parentWidget()->rect());

    rebuildBolt();
    elapsed_.restart();
    show();
    raise();
    update();
    frameTimer_.start();
}

bool LightningOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());

    return QWidget::eventFilter(watched, event);
}

void LightningOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    if (!elapsed_.isValid() || mainBolt_.size() < 2) return;

    const qreal progress = std::clamp(
        static_cast<qreal>(elapsed_.elapsed()) /
            static_cast<qreal>(durationMilliseconds_),
        0.0, 1.0);
    const qreal firstFlash = std::exp(-10.0 * progress);
    const qreal secondFlash = 0.28 * std::exp(
        -std::pow((progress - 0.18) / 0.055, 2.0));
    const qreal flashPower = std::min<qreal>(1.0,
                                             firstFlash + secondFlash);
    const qreal boltOpacity = progress < 0.16
        ? 1.0
        : std::pow(std::max<qreal>(0.0, (1.0 - progress) / 0.84), 1.55);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    QColor flashColor = dark ? QColor(112, 128, 255)
                             : QColor(80, 96, 220);
    flashColor.setAlphaF(flashPower * (dark ? 0.12 : 0.065));
    painter.fillRect(rect(), flashColor);

    QPainterPath completeBolt;
    appendBolt(completeBolt, mainBolt_);
    for (const BoltPath &branch : branches_) appendBolt(completeBolt, branch);

    const QColor outerColor = dark ? QColor(95, 115, 255, 44)
                                   : QColor(58, 75, 205, 34);
    const QColor middleColor = dark ? QColor(150, 165, 255, 120)
                                    : QColor(70, 82, 200, 105);
    const QColor coreColor = dark ? QColor(248, 250, 255, 245)
                                  : QColor(38, 48, 135, 225);

    painter.setOpacity(boltOpacity);
    painter.setPen(QPen(outerColor, 18.0, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(completeBolt);
    painter.setPen(QPen(middleColor, 7.0, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(completeBolt);
    painter.setPen(QPen(coreColor, 1.8, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(completeBolt);
}

void LightningOverlay::rebuildBolt()
{
    mainBolt_.clear();
    branches_.clear();

    const qreal overlayWidth = static_cast<qreal>(width());
    const qreal overlayHeight = static_cast<qreal>(height());
    if (overlayWidth < 1.0 || overlayHeight < 1.0) return;

    QRandomGenerator *generator = QRandomGenerator::global();
    constexpr int segmentCount = 24;
    const qreal margin = std::min<qreal>(24.0, overlayWidth * 0.08);
    qreal x = randomBetween(generator, overlayWidth * 0.40,
                            overlayWidth * 0.60);
    const qreal destinationX = randomBetween(generator, overlayWidth * 0.32,
                                              overlayWidth * 0.68);

    mainBolt_.reserve(segmentCount + 1);
    mainBolt_.append(QPointF(x, -8.0));
    for (int index = 1; index <= segmentCount; ++index)
    {
        const qreal depth = static_cast<qreal>(index) / segmentCount;
        const qreal y = -8.0 + (overlayHeight + 16.0) * depth;
        const qreal pull = (destinationX - x) * (0.035 + depth * 0.025);
        const qreal jitter = randomBetween(
            generator, -overlayWidth * 0.047, overlayWidth * 0.047) *
            (1.0 - depth * 0.28);
        x = std::clamp(x + pull + jitter, margin,
                       overlayWidth - margin);
        mainBolt_.append(QPointF(x, y));
    }

    for (int originIndex = 5; originIndex < segmentCount - 2;
         originIndex += 5)
    {
        BoltPath branch;
        QPointF point = mainBolt_[originIndex];
        branch.append(point);

        const qreal direction = generator->bounded(2) == 0 ? -1.0 : 1.0;
        const int branchSegments = 3 + generator->bounded(3);
        for (int index = 0; index < branchSegments; ++index)
        {
            point.rx() += direction * randomBetween(generator, 16.0, 34.0) +
                          randomBetween(generator, -7.0, 7.0);
            point.ry() += randomBetween(generator, 16.0, 34.0);
            point.setX(std::clamp(point.x(), margin,
                                  overlayWidth - margin));
            point.setY(std::min(point.y(), overlayHeight + 6.0));
            branch.append(point);
        }
        branches_.append(std::move(branch));
    }
}

void LightningOverlay::stop()
{
    frameTimer_.stop();
    elapsed_.invalidate();
    hide();
}
