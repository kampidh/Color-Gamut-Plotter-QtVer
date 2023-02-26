/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * Based on the Krita CIE Tongue Widget
 *
 * SPDX-FileCopyrightText: 2015 Wolthera van Hövell tot Westerflier <griffinvalley@gmail.com>
 *
 * Based on the Digikam CIE Tongue widget
 * SPDX-FileCopyrightText: 2006-2013 Gilles Caulier <caulier dot gilles at gmail dot com>
 *
 * Any source code are inspired from lprof project and
 * SPDX-FileCopyrightText: 1998-2001 Marti Maria
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include <QAction>
#include <QInputDialog>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVector3D>
#include <cmath>

#include <QApplication>
#include <QClipboard>

#include <QDebug>

#include "scatter2dchart.h"

static const double spectral_chromaticity[81][3] = {
    {0.1741, 0.0050}, // 380 nm
    {0.1740, 0.0050}, {0.1738, 0.0049}, {0.1736, 0.0049}, {0.1733, 0.0048}, {0.1730, 0.0048}, {0.1726, 0.0048},
    {0.1721, 0.0048}, {0.1714, 0.0051}, {0.1703, 0.0058}, {0.1689, 0.0069}, {0.1669, 0.0086}, {0.1644, 0.0109},
    {0.1611, 0.0138}, {0.1566, 0.0177}, {0.1510, 0.0227}, {0.1440, 0.0297}, {0.1355, 0.0399}, {0.1241, 0.0578},
    {0.1096, 0.0868}, {0.0913, 0.1327}, {0.0687, 0.2007}, {0.0454, 0.2950}, {0.0235, 0.4127}, {0.0082, 0.5384},
    {0.0039, 0.6548}, {0.0139, 0.7502}, {0.0389, 0.8120}, {0.0743, 0.8338}, {0.1142, 0.8262}, {0.1547, 0.8059},
    {0.1929, 0.7816}, {0.2296, 0.7543}, {0.2658, 0.7243}, {0.3016, 0.6923}, {0.3373, 0.6589}, {0.3731, 0.6245},
    {0.4087, 0.5896}, {0.4441, 0.5547}, {0.4788, 0.5202}, {0.5125, 0.4866}, {0.5448, 0.4544}, {0.5752, 0.4242},
    {0.6029, 0.3965}, {0.6270, 0.3725}, {0.6482, 0.3514}, {0.6658, 0.3340}, {0.6801, 0.3197}, {0.6915, 0.3083},
    {0.7006, 0.2993}, {0.7079, 0.2920}, {0.7140, 0.2859}, {0.7190, 0.2809}, {0.7230, 0.2770}, {0.7260, 0.2740},
    {0.7283, 0.2717}, {0.7300, 0.2700}, {0.7311, 0.2689}, {0.7320, 0.2680}, {0.7327, 0.2673}, {0.7334, 0.2666},
    {0.7340, 0.2660}, {0.7344, 0.2656}, {0.7346, 0.2654}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653},
    {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653},
    {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653}, {0.7347, 0.2653},
    {0.7347, 0.2653}, {0.7347, 0.2653} // 780 nm
};

class Q_DECL_HIDDEN Scatter2dChart::Private
{
public:
    bool needUpdatePixmap{false};
    bool isMouseHold{false};
    bool isDownscaled{false};
    QPainter m_painter;
    QPixmap m_pixmap;
    QVector<QVector3D> m_dArray;
    QVector<QVector3D> m_dOutGamut;
    QVector2D m_dWhitePoint;
    QVector<QColor> m_dColor;
    int m_particleSize;
    int m_particleSizeStored;
    int m_drawnParticles{0};
    int m_neededParticles{0};
    double m_zoomRatio{1.1};
    int m_offsetX{100};
    int m_offsetY{50};
    QPoint m_lastPos{};
    int m_dArrayIterSize = 1;
    QTimer *m_scrollTimer;

    QAction *setZoom;
    QAction *setOrigin;
    QAction *copyOrigAndZoom;
    QAction *pasteOrigAndZoom;
    QAction *drawLabels;
    QAction *drawGrids;
    QAction *drawSrgbGamut;
    QAction *drawImgGamut;
    QAction *setStaticDownscale;
    QAction *setAlpha;
    QAction *setAntiAliasing;

    bool enableLabels{true};
    bool enableGrids{true};
    bool enableSrgbGamut{true};
    bool enableImgGamut{true};
    bool enableStaticDownscale{false};
    bool enableAA{false};

    QClipboard *m_clipb;
};

Scatter2dChart::Scatter2dChart(QWidget *parent)
    : QWidget{parent}
    , d(new Private)
{
    // prepare timer for downscaling
    d->m_scrollTimer = new QTimer(this);
    d->m_scrollTimer->setSingleShot(true);
    connect(d->m_scrollTimer, &QTimer::timeout, this, &Scatter2dChart::whenScrollTimerEnds);

    d->m_clipb = QApplication::clipboard();

    d->setZoom = new QAction("Set zoom...");
    connect(d->setZoom, &QAction::triggered, this, &Scatter2dChart::changeZoom);

    d->setOrigin = new QAction("Set origin...");
    connect(d->setOrigin, &QAction::triggered, this, &Scatter2dChart::changeOrigin);

    d->copyOrigAndZoom = new QAction("Copy origin and zoom");
    connect(d->copyOrigAndZoom, &QAction::triggered, this, &Scatter2dChart::copyOrigAndZoom);

    d->pasteOrigAndZoom = new QAction("Paste origin and zoom");
    connect(d->pasteOrigAndZoom, &QAction::triggered, this, &Scatter2dChart::pasteOrigAndZoom);

    d->drawLabels = new QAction("Draw labels");
    d->drawLabels->setCheckable(true);
    d->drawLabels->setChecked(d->enableLabels);
    connect(d->drawLabels, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawGrids = new QAction("Draw grids and spectral line");
    d->drawGrids->setCheckable(true);
    d->drawGrids->setChecked(d->enableGrids);
    connect(d->drawGrids, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawSrgbGamut = new QAction("Draw sRGB gamut");
    d->drawSrgbGamut->setCheckable(true);
    d->drawSrgbGamut->setChecked(d->enableSrgbGamut);
    connect(d->drawSrgbGamut, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawImgGamut = new QAction("Draw image gamut");
    d->drawImgGamut->setCheckable(true);
    d->drawImgGamut->setChecked(d->enableImgGamut);
    connect(d->drawImgGamut, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->setStaticDownscale = new QAction("Use static downscaling");
    d->setStaticDownscale->setCheckable(true);
    d->setStaticDownscale->setChecked(d->enableStaticDownscale);
    connect(d->setStaticDownscale, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->setAntiAliasing = new QAction("Enable AntiAliasing (slow!)");
    d->setAntiAliasing->setToolTip("Only use for final render, as it only have very small impact in visual quality.");
    d->setAntiAliasing->setCheckable(true);
    d->setAntiAliasing->setChecked(d->enableAA);
    connect(d->setAntiAliasing, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->setAlpha = new QAction("Set alpha / brightness...");
    connect(d->setAlpha, &QAction::triggered, this, &Scatter2dChart::changeAlpha);
}

Scatter2dChart::~Scatter2dChart()
{
    delete d;
}

void Scatter2dChart::addDataPoints(QVector<QVector3D> &dArray, QVector<QColor> &dColor, int size)
{
    d->needUpdatePixmap = true;
    d->m_dArray = dArray;
    d->m_dColor = dColor;
    d->m_particleSize = size;
    d->m_particleSizeStored = size;
    d->m_neededParticles = dArray.size();

    // set alpha based on numpoints
    const double alphaToLerp =
        std::min(std::max(1.0 - (d->m_dArray.size() - 50000.0) / (5000000.0 - 50000.0), 0.0), 1.0);
    const double alphaLerpToGamma = 0.1 + ((1.0 - 0.1) * std::pow(alphaToLerp, 5.5));

    for (int i = 0; i < d->m_dArray.size(); i++) {
        d->m_dColor[i].setAlphaF(alphaLerpToGamma);
    }
}

void Scatter2dChart::addGamutOutline(QVector<QVector3D> &dOutGamut, QVector2D &dWhitePoint)
{
    d->m_dOutGamut = dOutGamut;
    d->m_dWhitePoint = dWhitePoint;
}

QPoint Scatter2dChart::mapPoint(QPointF xy)
{
    // Maintain ascpect ratio, otherwise use width() on X
    return QPoint((static_cast<int>((xy.x() * d->m_zoomRatio) * (height() / devicePixelRatioF())) + d->m_offsetX),
                  (static_cast<int>((height() / devicePixelRatioF())
                                    - ((xy.y() * d->m_zoomRatio) * (height() / devicePixelRatioF())))
                   - d->m_offsetY));
}

void Scatter2dChart::drawDataPoints()
{
    // prepare window dimension
    const double scaleHRatio = height() / devicePixelRatioF();
    const double scaleWRatio = width() / devicePixelRatioF();
    const double originX = (d->m_offsetX / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double originY = (d->m_offsetY / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double maxX = ((d->m_offsetX - scaleWRatio) / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double maxY = ((d->m_offsetY - scaleHRatio) / scaleHRatio) / d->m_zoomRatio * -1.0;

    d->m_painter.save();
    if ((d->m_dArray.size() < 500000 && d->m_dArrayIterSize == 1) || d->enableAA) {
        if (d->enableAA) {
            d->m_painter.setRenderHint(QPainter::HighQualityAntialiasing);
        } else {
            d->m_painter.setRenderHint(QPainter::Antialiasing);
        }
    }
    d->m_painter.setPen(Qt::transparent);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_Lighten);

    d->m_drawnParticles = 0;

    if (!d->enableStaticDownscale) {
        // only for dynamic downscaling, calculate how much points is needed for onscreen rendering
        d->m_neededParticles = 0;
        for (int i = 0; i < d->m_dArray.size(); i++) {
            if ((d->m_dArray.at(i).x() > originX && d->m_dArray.at(i).x() < maxX)
                && (d->m_dArray.at(i).y() > originY && d->m_dArray.at(i).y() < maxY)) {
                d->m_neededParticles++;
            }
        }
    }

    for (int i = 0; i < d->m_dArray.size(); i += d->m_dArrayIterSize) {
        // only draw what's inside the window and skip offscreen points
        if ((d->m_dArray.at(i).x() > originX && d->m_dArray.at(i).x() < maxX)
            && (d->m_dArray.at(i).y() > originY && d->m_dArray.at(i).y() < maxY)) {
            if (d->m_dArrayIterSize > 1) {
                d->m_painter.setBrush(
                    QColor(d->m_dColor.at(i).red(), d->m_dColor.at(i).green(), d->m_dColor.at(i).blue(), 160));
            } else {
                d->m_painter.setBrush(d->m_dColor.at(i));
            }

            const QPoint map = mapPoint(QPointF(d->m_dArray.at(i).x(), d->m_dArray.at(i).y()));

            d->m_painter.drawEllipse(map.x() - (d->m_particleSize / 2),
                                     map.y() - (d->m_particleSize / 2),
                                     d->m_particleSize,
                                     d->m_particleSize);
            d->m_drawnParticles++;
        }
    }

    d->m_painter.restore();
}

void Scatter2dChart::drawSpectralLine()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    QPen pn;
    pn.setColor(QColor(64, 64, 64));
    pn.setWidth(1);
    d->m_painter.setPen(pn);

    QPoint mapB;
    QPoint mapC;

    for (int x = 380; x <= 700; x += 5) {
        int ix = (x - 380) / 5;
        const QPoint map = mapPoint(QPointF(spectral_chromaticity[ix][0], spectral_chromaticity[ix][1]));

        if (x > 380) {
            d->m_painter.drawLine(mapB, map);
        } else {
            mapC = map;
        }
        mapB = map;
    }

    d->m_painter.drawLine(mapB, mapC);
    d->m_painter.restore();
}

void Scatter2dChart::drawSrgbTriangle()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_Plus);
    QPen pn;
    pn.setColor(QColor(128, 128, 128, 128));
    pn.setWidth(1);
    d->m_painter.setPen(pn);

    QPoint mapR = mapPoint(QPointF(0.64, 0.33));
    QPoint mapG = mapPoint(QPointF(0.3, 0.6));
    QPoint mapB = mapPoint(QPointF(0.15, 0.06));
    QPoint mapW = mapPoint(QPointF(0.3127, 0.3290));

    d->m_painter.drawLine(mapR, mapG);
    d->m_painter.drawLine(mapG, mapB);
    d->m_painter.drawLine(mapB, mapR);

    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    d->m_painter.setBrush(Qt::white);

    d->m_painter.drawEllipse(mapW.x() - (4 / 2), mapW.y() - (4 / 2), 4, 4);

    d->m_painter.restore();
}

void Scatter2dChart::drawGamutTriangleWP()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_Plus);

    QPen pn;
    pn.setColor(QColor(128, 0, 0, 128));
    pn.setWidth(2);
    d->m_painter.setPen(pn);

    d->m_painter.setBrush(Qt::transparent);

    const int pointSize = 3;

    QPolygonF gamutPoly;

    for (int i = 0; i < d->m_dOutGamut.size(); i++) {
        gamutPoly << mapPoint(QPointF(d->m_dOutGamut.at(i).x(), d->m_dOutGamut.at(i).y()));
    }

    d->m_painter.drawPolygon(gamutPoly);

    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QPoint mapW = mapPoint(QPointF(d->m_dWhitePoint.x(), d->m_dWhitePoint.y()));
    d->m_painter.setBrush(Qt::white);
    d->m_painter.drawEllipse(mapW.x() - (4 / 2), mapW.y() - (4 / 2), 4, 4);

    d->m_painter.restore();
}

void Scatter2dChart::drawGrids()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);

    QPen mainAxis;
    mainAxis.setWidth(1);
    mainAxis.setColor(QColor(128, 128, 160, 190));
    mainAxis.setStyle(Qt::SolidLine);

    QPen mainGrid;
    mainGrid.setWidth(1);
    mainGrid.setColor(QColor(128, 128, 128, 96));
    mainGrid.setStyle(Qt::DotLine);

    QPen subGrid;
    subGrid.setWidth(1);
    subGrid.setColor(QColor(128, 128, 128, 64));
    subGrid.setStyle(Qt::DotLine);

    d->m_painter.setFont(QFont("Courier", (d->m_zoomRatio * 12.0), QFont::Medium));
    d->m_painter.setBrush(Qt::transparent);

    const float labelBias = -0.02;
    const float fromPos = -0.5;
    const float toPos = 1;

    for (int i = -5; i < 10; i++) {
        if (i == 0) {
            d->m_painter.setPen(mainAxis);

            d->m_painter.drawText(mapPoint(QPointF(labelBias, labelBias)), "0");

            d->m_painter.drawLine(mapPoint(QPointF(i / 10.0, fromPos)), mapPoint(QPointF(i / 10.0, toPos)));
            d->m_painter.drawLine(mapPoint(QPointF(fromPos, i / 10.0)), mapPoint(QPointF(toPos, i / 10.0)));
        } else {
            d->m_painter.setPen(mainGrid);
            d->m_painter.drawLine(mapPoint(QPointF(i / 10.0, fromPos)), mapPoint(QPointF(i / 10.0, toPos)));
            d->m_painter.drawLine(mapPoint(QPointF(fromPos, i / 10.0)), mapPoint(QPointF(toPos, i / 10.0)));

            QString lb(QString::number(i / 10.0, 'G', 2));
            d->m_painter.setPen(mainAxis);
            d->m_painter.drawText(mapPoint(QPointF((i / 10.0) - 0.012, labelBias)), lb);
            d->m_painter.drawText(mapPoint(QPointF(labelBias - 0.012, i / 10.0 - 0.002)), lb);
        }
        if (d->m_zoomRatio > 2.0) {
            for (int j = 1; j < 10; j++) {
                d->m_painter.setPen(subGrid);
                d->m_painter.drawLine(mapPoint(QPointF((i / 10.0) - (j / 100.0), fromPos)),
                                      mapPoint(QPointF(i / 10.0 - (j / 100.0), toPos)));
                d->m_painter.drawLine(mapPoint(QPointF(fromPos, (i / 10.0) - (j / 100.0))),
                                      mapPoint(QPointF(toPos, (i / 10.0) - (j / 100.0))));
            }
        }
    }

    d->m_painter.restore();
}

void Scatter2dChart::drawLabels()
{
    d->m_painter.save();
//    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setPen(QPen(Qt::lightGray));
    d->m_painter.setFont(QFont("Courier", 11, QFont::Medium));
    const double scaleHRatio = height() / devicePixelRatioF();
    const double originX = (d->m_offsetX / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double originY = (d->m_offsetY / scaleHRatio) / d->m_zoomRatio * -1.0;
    const QString legends = QString("Pixels: %4 (total)| %5 (%6)\nOrigin: x:%1 | y:%2\nZoom: %3\%")
                                .arg(QString::number(originX, 'f', 6),
                                     QString::number(originY, 'f', 6),
                                     QString::number(d->m_zoomRatio * 100.0, 'f', 2),
                                     QString::number(d->m_dArray.size()),
                                     QString::number(d->m_drawnParticles),
                                     QString(d->isDownscaled ? "rendering..." : "rendered"));
    d->m_painter.drawText(rect(), Qt::AlignBottom | Qt::AlignLeft, legends);

    d->m_painter.restore();
}

void Scatter2dChart::doUpdate()
{
    d->needUpdatePixmap = false;
    d->m_pixmap = QPixmap(size() * devicePixelRatioF());
    d->m_pixmap.setDevicePixelRatio(devicePixelRatioF());
    d->m_pixmap.fill(Qt::black);

    d->m_painter.begin(&d->m_pixmap);

    if (d->enableGrids) {
        drawGrids();
        drawSpectralLine();
    }
    drawDataPoints();
    if (d->enableSrgbGamut) {
        drawSrgbTriangle();
    }
    if (d->enableImgGamut) {
        drawGamutTriangleWP();
    }
    if (d->enableLabels) {
        drawLabels();
    }

    d->m_painter.end();
}

void Scatter2dChart::paintEvent(QPaintEvent *)
{
    // draw something
    QPainter p(this);
    if (d->needUpdatePixmap) {
        doUpdate();
    }
    p.drawPixmap(0, 0, d->m_pixmap);
}

void Scatter2dChart::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // downscale
    drawDownscaled(500);
    d->needUpdatePixmap = true;
}

void Scatter2dChart::wheelEvent(QWheelEvent *event)
{
    const QPoint numDegrees = event->angleDelta();

    // exponential zooming, so that it will feel linear especially
    // on high zooom rate
    const double zoomIncrement = (numDegrees.y() / 1200.0) * d->m_zoomRatio;

    event->accept();

    // Zoom cap (around 80% - 20000%)
    if (d->m_zoomRatio + zoomIncrement > 0.8 && d->m_zoomRatio + zoomIncrement < 200.0) {
        drawDownscaled(500);

        // window screen space -> absolute space for a smoother zooming at cursor point
        // I probably reinventing the wheel here but well...
        const float windowAspectRatio = width() * 1.0 / height() * 1.0;
        const QPointF scenepos = QPointF((event->position().x() / (width() / devicePixelRatioF()) * windowAspectRatio),
                                         1.0 - (event->position().y() / (height() / devicePixelRatioF())));
        const int windowUnit = height() / devicePixelRatioF();
        const float windowsAbsUnit = windowUnit * d->m_zoomRatio;
        const QPointF windowSceneCurScaledPos = scenepos / d->m_zoomRatio;
        const QPointF windowSceneCurAbsPos =
            windowSceneCurScaledPos - QPointF(d->m_offsetX / windowsAbsUnit, d->m_offsetY / windowsAbsUnit);

        d->m_zoomRatio += zoomIncrement;

        d->m_offsetX -= static_cast<int>(zoomIncrement * (windowSceneCurAbsPos.x() * windowUnit));
        d->m_offsetY -= static_cast<int>(zoomIncrement * (windowSceneCurAbsPos.y() * windowUnit));

        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        d->m_lastPos = event->pos();
    }
}

void Scatter2dChart::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        d->isMouseHold = true;
        drawDownscaled(500);

        const QPoint delposs(event->pos() - d->m_lastPos);
        d->m_lastPos = event->pos();

        d->m_offsetX += delposs.x();
        d->m_offsetY -= delposs.y();

        // qDebug() << "offsetX:" << (d->m_offsetX / (height() / devicePixelRatioF())) / d->m_zoomRatio;
        // qDebug() << "offsetY:" << (d->m_offsetY / (height() / devicePixelRatioF())) / d->m_zoomRatio;

        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::mouseReleaseEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton) && d->isMouseHold) {
        d->isMouseHold = false;
        drawDownscaled(500);
        d->needUpdatePixmap = true;
        update();
    }
    // reserved
}

void Scatter2dChart::contextMenuEvent(QContextMenuEvent *event)
{
    const int mouseXPos = event->pos().x();
    const int mouseYPos = height() - event->pos().y();

    const double scaleRatio = height() / devicePixelRatioF();
    const QString sizePos = QString("Cursor: x: %1 | y: %2 | %3\%")
                                .arg(QString::number(((d->m_offsetX - mouseXPos) / scaleRatio) / d->m_zoomRatio * -1.0, 'f', 6),
                                     QString::number(((d->m_offsetY - mouseYPos) / scaleRatio) / d->m_zoomRatio * -1.0, 'f', 6),
                                     QString::number(d->m_zoomRatio * 100.0, 'f', 2));
    QMenu menu(this);
    menu.addAction(sizePos);
    menu.addSeparator();
    menu.addAction(d->setZoom);
    menu.addAction(d->setOrigin);
    menu.addSeparator();
    menu.addAction(d->copyOrigAndZoom);
    menu.addAction(d->pasteOrigAndZoom);
    menu.addSeparator();
    menu.addAction(d->drawLabels);
    menu.addAction(d->drawGrids);
    menu.addAction(d->drawSrgbGamut);
    menu.addAction(d->drawImgGamut);
    menu.addSeparator();
    menu.addAction(d->setStaticDownscale);
    menu.addAction(d->setAntiAliasing);
    menu.addAction(d->setAlpha);
    menu.exec(event->globalPos());
}

void Scatter2dChart::changeZoom()
{
    const double currentZoom = d->m_zoomRatio * 100.0;
    const double scaleRatio = height() / devicePixelRatioF();
    const double currentXOffset = (d->m_offsetX / scaleRatio) / d->m_zoomRatio;
    const double currentYOffset = (d->m_offsetY / scaleRatio) / d->m_zoomRatio;
    bool isZoomOkay(false);
    const double setZoom =
        QInputDialog::getDouble(this, "Set zoom", "Zoom", currentZoom, 80.0, 20000.0, 1, &isZoomOkay);
    if (isZoomOkay) {
        d->m_zoomRatio = setZoom / 100.0;
        d->m_offsetX = (currentXOffset * d->m_zoomRatio) * scaleRatio;
        d->m_offsetY = (currentYOffset * d->m_zoomRatio) * scaleRatio;
        drawDownscaled(200);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::changeOrigin()
{
    const double scaleRatio = height() / devicePixelRatioF();
    const double currentXOffset = (d->m_offsetX / scaleRatio) / d->m_zoomRatio * -1.0;
    const double currentYOffset = (d->m_offsetY / scaleRatio) / d->m_zoomRatio * -1.0;
    bool isXOkay(false);
    bool isYOkay(false);
    const double setX = QInputDialog::getDouble(this, "Set origin", "Origin X", currentXOffset, -0.1, 1.0, 5, &isXOkay);
    const double setY = QInputDialog::getDouble(this, "Set origin", "Origin Y", currentYOffset, -0.1, 1.0, 5, &isYOkay);
    if (isXOkay && isYOkay) {
        const double setXToVal = ((setX * -1.0) * d->m_zoomRatio) * scaleRatio;
        const double setYToVal = ((setY * -1.0) * d->m_zoomRatio) * scaleRatio;
        d->m_offsetX = setXToVal;
        d->m_offsetY = setYToVal;
        drawDownscaled(200);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::copyOrigAndZoom()
{
    const double currentZoom = d->m_zoomRatio * 100.0;
    const double scaleRatio = height() / devicePixelRatioF();
    const double currentXOffset = (d->m_offsetX / scaleRatio) / d->m_zoomRatio * -1.0;
    const double currentYOffset = (d->m_offsetY / scaleRatio) / d->m_zoomRatio * -1.0;

    const QString clipText = QString("QtGamutPlotter:%1;%2;%3")
                                 .arg(QString::number(currentZoom, 'f', 3),
                                      QString::number(currentXOffset, 'f', 8),
                                      QString::number(currentYOffset, 'f', 8));

    d->m_clipb->setText(clipText);
}

void Scatter2dChart::pasteOrigAndZoom()
{
    QString fromClip = d->m_clipb->text();
    if (fromClip.contains("QtGamutPlotter:")) {
        const double scaleRatio = height() / devicePixelRatioF();
        const QString cleanClip = fromClip.replace("QtGamutPlotter:", "");
        const QStringList parsed = cleanClip.split(";");

        if (parsed.size() == 3) {
            const double setZoom = parsed.at(0).toDouble() / 100.0;
            if (setZoom > 0.8 && setZoom < 200.0) {
                d->m_zoomRatio = setZoom;
            }

            const double setX = parsed.at(1).toDouble() * -1.0;
            const double setY = parsed.at(2).toDouble() * -1.0;

            if ((setX > -1.0 && setX < 1.0) && (setY > -1.0 && setY < 1.0)) {
                const double setXToVal = (setX * d->m_zoomRatio) * scaleRatio;
                const double setYToVal = (setY * d->m_zoomRatio) * scaleRatio;
                d->m_offsetX = setXToVal;
                d->m_offsetY = setYToVal;

                drawDownscaled(50);
                d->needUpdatePixmap = true;
                update();
            }
        }
    }
}

void Scatter2dChart::changeProperties()
{
    d->enableLabels = d->drawLabels->isChecked();
    d->enableGrids = d->drawGrids->isChecked();
    d->enableSrgbGamut = d->drawSrgbGamut->isChecked();
    d->enableImgGamut = d->drawImgGamut->isChecked();
    d->enableStaticDownscale = d->setStaticDownscale->isChecked();
    d->enableAA = d->setAntiAliasing->isChecked();

    drawDownscaled(100);
    d->needUpdatePixmap = true;
    update();
}

void Scatter2dChart::changeAlpha()
{
    const double currentAlpha = d->m_dColor.at(0).alphaF();
    bool isAlphaOkay(false);
    const double setAlpha =
        QInputDialog::getDouble(this, "Set alpha", "Per-particle alpha", currentAlpha, 0.1, 1.0, 2, &isAlphaOkay);
    if (isAlphaOkay) {
        for (int i = 0; i < d->m_dColor.size(); i++) {
            d->m_dColor[i].setAlphaF(setAlpha);
        }

        drawDownscaled(100);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::whenScrollTimerEnds()
{
    // render at full again
    if (d->isMouseHold)
        return;
    d->isDownscaled = false;
    d->m_dArrayIterSize = 1;
    d->m_particleSize = d->m_particleSizeStored;
    d->needUpdatePixmap = true;
    update();
}

void Scatter2dChart::drawDownscaled(int delayms)
{
    // downscale
    d->isDownscaled = true;
    if(!d->isMouseHold) {
        d->m_scrollTimer->start(delayms);
    }

    // dynamic downscaling
    if (!d->enableStaticDownscale) {
        const int iterSize = d->m_neededParticles / 50000; // 50k maximum particles
        if (iterSize > 1) {
            d->m_dArrayIterSize = iterSize;
            d->m_particleSize = 4;
        } else {
            d->m_dArrayIterSize = 1;
            d->m_particleSize = 4;
        }
    } else {
        // static downscaling
        if (d->m_dArray.size() > 2000000) {
            d->m_dArrayIterSize = 100;
        } else if (d->m_dArray.size() > 500000) {
            d->m_dArrayIterSize = 50;
        } else {
            d->m_dArrayIterSize = 10;
        }
        d->m_particleSize = 4;
    }
}

void Scatter2dChart::resetCamera()
{
    drawDownscaled(500);

    d->m_zoomRatio = 1.1;
    d->m_offsetX = 100;
    d->m_offsetY = 50;

    d->needUpdatePixmap = true;
    update();
}
