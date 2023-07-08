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

#include <QtConcurrent>
#include <QFuture>

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

// adaptive downsampling params, hardcoded
static const int adaptiveIterMaxPixels = 50000;
static const int adaptiveIterMaxRenderedPoints = 25000;

class Q_DECL_HIDDEN Scatter2dChart::Private
{
public:
    bool needUpdatePixmap{false};
    bool isMouseHold{false};
    bool isDownscaled{false};
    bool useBucketRender{false};
    QPainter m_painter;
    QPixmap m_pixmap;
    QVector<QVector3D> m_dOutGamut;
    QVector2D m_dWhitePoint;
    int m_particleSize{0};
    int m_particleSizeStored{0};
    int m_drawnParticles{0};
    int m_neededParticles{0};
    int adaptiveIterVal{0};
    double m_zoomRatio{1.1};
    double m_pixmapSize{1.0};
    int m_offsetX{100};
    int m_offsetY{50};
    QPoint m_lastPos{};
    int m_dArrayIterSize = 1;
    QTimer *m_scrollTimer;
    QFont m_labelFont;

    QVector<ColorPoint> m_cPoints;
    int m_idealThrCount = 0;

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
    QAction *setParticleSize;
    QAction *setPixmapSize;

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

    d->m_labelFont = QFont("Courier", 11, QFont::Medium);

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

    d->setParticleSize = new QAction("Set particle size...");
    connect(d->setParticleSize, &QAction::triggered, this, &Scatter2dChart::changeParticleSize);

    d->setPixmapSize = new QAction("Set render scaling...");
    connect(d->setPixmapSize, &QAction::triggered, this, &Scatter2dChart::changePixmapSize);

    d->setAlpha = new QAction("Set alpha / brightness...");
    connect(d->setAlpha, &QAction::triggered, this, &Scatter2dChart::changeAlpha);

    if (QThread::idealThreadCount() > 1) {
        d->m_idealThrCount = QThread::idealThreadCount();
    } else {
        d->m_idealThrCount = 1;
    }
}

Scatter2dChart::~Scatter2dChart()
{
    delete d;
}

void Scatter2dChart::addDataPoints(QVector<QVector3D> &dArray, QVector<QColor> &dColor, int size)
{
    d->needUpdatePixmap = true;
    d->m_particleSize = size;
    d->m_particleSizeStored = size;
    d->m_neededParticles = dArray.size();

    // set alpha based on numpoints
    const double alphaToLerp =
        std::min(std::max(1.0 - (dArray.size() - 50000.0) / (5000000.0 - 50000.0), 0.0), 1.0);
    const double alphaLerpToGamma = 0.1 + ((1.0 - 0.1) * std::pow(alphaToLerp, 5.5));

    for (int i = 0; i < dArray.size(); i++) {
        d->m_cPoints.append({dArray.at(i), dColor.at(i)});
        d->m_cPoints.last().second.setAlphaF(alphaLerpToGamma);
    }

    if (d->m_cPoints.size() > adaptiveIterMaxPixels) {
        d->adaptiveIterVal = d->m_cPoints.size() / adaptiveIterMaxPixels;
    } else {
        d->adaptiveIterVal = 1;
    }
}

void Scatter2dChart::addGamutOutline(QVector<QVector3D> &dOutGamut, QVector2D &dWhitePoint)
{
    d->m_dOutGamut = dOutGamut;
    d->m_dWhitePoint = dWhitePoint;
}

QPointF Scatter2dChart::mapPoint(QPointF xy)
{
    // Maintain ascpect ratio, otherwise use width() on X
    return QPointF(((xy.x() * d->m_zoomRatio) * d->m_pixmap.height() + d->m_offsetX),
                   ((d->m_pixmap.height() - ((xy.y() * d->m_zoomRatio) * d->m_pixmap.height())) - d->m_offsetY));
}

/*
 * Data point rendering modes:
 * - Bucket, mostly slower but lighter in RAM
 * - Multipass, mostly faster but RAM heavy especially with large upscaling
 */
static const int bucketDefaultSize = 512;
static const int bucketDownscaledSize = 1024;
//static const int bucketPadding = 2;
//static const int estimatedDownscale = 100;
static const int maxPixelsBeforeBucket = 4000000;

void Scatter2dChart::drawDataPoints()
{
    // prepare window dimension
    const double scaleHRatio = d->m_pixmap.height();
    const double scaleWRatio = d->m_pixmap.width();
    const double originX = (d->m_offsetX / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double originY = (d->m_offsetY / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double maxX = ((d->m_offsetX - scaleWRatio) / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double maxY = ((d->m_offsetY - scaleHRatio) / scaleHRatio) / d->m_zoomRatio * -1.0;

    d->m_painter.save();
    d->m_painter.setCompositionMode(QPainter::CompositionMode_Lighten);

    d->m_drawnParticles = 0;

    // calculate an estimate how much points is needed for onscreen rendering
    d->m_neededParticles = 0;
    for (int i = 0; i < d->m_cPoints.size(); i += d->adaptiveIterVal) {
        if ((d->m_cPoints.at(i).first.x() > originX && d->m_cPoints.at(i).first.x() < maxX)
            && (d->m_cPoints.at(i).first.y() > originY && d->m_cPoints.at(i).first.y() < maxY)) {
            d->m_neededParticles++;
        }
    }
    d->m_neededParticles = d->m_neededParticles * d->adaptiveIterVal;

    const int pixmapPixSize = d->m_pixmap.width() * d->m_pixmap.height();

    if (pixmapPixSize > maxPixelsBeforeBucket) {
        d->useBucketRender = true;
    } else {
        d->useBucketRender = false;
    }

    const int bucketSize = (d->isDownscaled ? bucketDownscaledSize : bucketDefaultSize);
    const int bucketPadding = d->m_particleSize;
    const QSize workerDim = [&]() {
        if (d->useBucketRender) {
            return QSize(bucketSize, bucketSize);
        }
        return d->m_pixmap.size();
    }();

    // internal function for painting the chunks concurrently
    std::function<QPixmap(const QVector<ColorPointMapped> &)> paintInChunk = [&](const QVector<ColorPointMapped> &chunk) {
        if (chunk.isEmpty()) {
            return QPixmap();
        }

        QPixmap tempMap(workerDim);
        tempMap.fill(Qt::transparent);
        QPainter tempPainterMap;

        tempPainterMap.begin(&tempMap);

        if (!d->isDownscaled && d->enableAA) {
            tempPainterMap.setRenderHint(QPainter::Antialiasing);
        }
        tempPainterMap.setPen(Qt::NoPen);
        tempPainterMap.setCompositionMode(QPainter::CompositionMode_Lighten);

        for (const ColorPointMapped &cp : chunk) {
            if (d->isDownscaled) {
                tempPainterMap.setBrush(QColor(cp.second.red(), cp.second.green(), cp.second.blue(), 160));
            } else {
                tempPainterMap.setBrush(cp.second);
            }

            if (d->enableAA && !d->isDownscaled) {
                tempPainterMap.drawEllipse(cp.first, d->m_particleSize / 2.0, d->m_particleSize / 2.0);
            } else {
                tempPainterMap.drawEllipse(cp.first.toPoint(), d->m_particleSize / 2, d->m_particleSize / 2);
            }
        }

        tempPainterMap.end();

        return tempMap;
    };

    QVector<QVector<ColorPointMapped>> fragmentedColPoints;
    QVector<ColorPointMapped> temporaryColPoints;

    // multipass param
    const int thrCount = (d->isDownscaled ? 2 : d->m_idealThrCount);
    const int chunkSize = d->m_neededParticles / thrCount;

    // bucket param
    const int bucketWNum = std::ceil(d->m_pixmap.width() / (bucketSize * 1.0));
    const int bucketHNum = std::ceil(d->m_pixmap.height() / (bucketSize * 1.0));
    const int bucketTotalNum = bucketWNum * bucketHNum;

    if (d->useBucketRender) {
        fragmentedColPoints.resize(bucketTotalNum);
    }

    // divide to chunks
    for (int i = 0; i < d->m_cPoints.size(); i += d->m_dArrayIterSize) {
        if (d->useBucketRender) {
            const QPointF map = mapPoint(QPointF(d->m_cPoints.at(i).first.x(), d->m_cPoints.at(i).first.y()));
            bool isDataWritten = false;

            // iterate over the buckets for each points
            for (int h = 0; h < bucketHNum; h++) {
                const int hAbsPos = h * bucketWNum;
                for (int w = 0; w < bucketWNum; w++) {
                    const int bucketAbsPos = hAbsPos + w;
                    const int orX = w * bucketSize;
                    const int orY = h * bucketSize;
                    const int maxX = orX + bucketSize;
                    const int maxY = orY + bucketSize;
                    if ((map.x() > orX - bucketPadding && map.x() < maxX + (bucketPadding * 2))
                        && (map.y() > orY - bucketPadding && map.y() < maxY + (bucketPadding * 2))
                        && (map.x() > 0 && map.x() < scaleWRatio)
                        && (map.y() > 0 && map.y() < scaleHRatio)) {
                        isDataWritten = true;
                        const QPointF mapAdj = QPointF(map.x() - orX, map.y() - orY);
                        fragmentedColPoints[bucketAbsPos].append({mapAdj, d->m_cPoints.at(i).second});
                    }
                }
            }
            // make sure padding data isn't counted
            if (isDataWritten) {
                d->m_drawnParticles++;
            }
        } else {
            // mutipass
            if ((d->m_cPoints.at(i).first.x() > originX && d->m_cPoints.at(i).first.x() < maxX)
                && (d->m_cPoints.at(i).first.y() > originY && d->m_cPoints.at(i).first.y() < maxY)) {
                    const QPointF map = mapPoint(QPointF(d->m_cPoints.at(i).first.x(), d->m_cPoints.at(i).first.y()));
                    temporaryColPoints.append({map, d->m_cPoints.at(i).second});
                    d->m_drawnParticles++;
            }

            // add passes when a chunk is filled + end chunk
            if (chunkSize > 0 && temporaryColPoints.size() == chunkSize) {
                fragmentedColPoints.append(temporaryColPoints);
                temporaryColPoints.clear();
            } else if (i >= (d->m_cPoints.size() - d->m_dArrayIterSize)) {
                fragmentedColPoints.append(temporaryColPoints);
                temporaryColPoints.clear();
            }
        }
    }

    QFuture<QPixmap> future = QtConcurrent::mapped(fragmentedColPoints, paintInChunk);

    future.waitForFinished();

    QPixmap temp(d->m_pixmap.size());
    temp.fill(Qt::transparent);
    QPainter tempPainter;

    tempPainter.begin(&temp);

    if (d->useBucketRender) {
        for (int h = 0; h < bucketHNum; h++) {
            const int hAbsPos = h * bucketWNum;
            for (int w = 0; w < bucketWNum; w++) {
                const int bucketAbsPos = hAbsPos + w;
                const int orX = w * bucketSize;
                const int orY = h * bucketSize;
                const QRect bounds(orX, orY, bucketSize, bucketSize);
                if (!future.resultAt(bucketAbsPos).isNull()) {
                    tempPainter.drawPixmap(bounds, future.resultAt(bucketAbsPos));
                }
            }
        }
    } else {
        tempPainter.setCompositionMode(QPainter::CompositionMode_Lighten);
        for (auto it = future.constBegin(); it != future.constEnd(); it++) {
            tempPainter.drawPixmap(temp.rect(), *it);
        }
    }

    tempPainter.end();

    d->m_painter.drawPixmap(d->m_pixmap.rect(), temp);

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

    QPointF mapB;
    QPointF mapC;

    for (int x = 380; x <= 700; x += 5) {
        int ix = (x - 380) / 5;
        const QPointF map = mapPoint(QPointF(spectral_chromaticity[ix][0], spectral_chromaticity[ix][1]));

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

    QPointF mapR = mapPoint(QPointF(0.64, 0.33));
    QPointF mapG = mapPoint(QPointF(0.3, 0.6));
    QPointF mapB = mapPoint(QPointF(0.15, 0.06));
    QPointF mapW = mapPoint(QPointF(0.3127, 0.3290));

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
    const QPointF mapW = mapPoint(QPointF(d->m_dWhitePoint.x(), d->m_dWhitePoint.y()));
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
    d->m_labelFont.setPointSize(std::pow(d->m_pixmapSize, 2) * 11);
    d->m_painter.setFont(d->m_labelFont);
    const double scaleHRatio = d->m_pixmap.height() / devicePixelRatioF();
    const double originX = (d->m_offsetX / scaleHRatio) / d->m_zoomRatio * -1.0;
    const double originY = (d->m_offsetY / scaleHRatio) / d->m_zoomRatio * -1.0;
    const QString legends = QString("Pixels: %4 (total)| %5 (%6, %7)\nOrigin: x:%1 | y:%2\nZoom: %3\%")
                                .arg(QString::number(originX, 'f', 6),
                                     QString::number(originY, 'f', 6),
                                     QString::number(d->m_zoomRatio * 100.0, 'f', 2),
                                     QString::number(d->m_cPoints.size()),
                                     QString::number(d->m_drawnParticles),
                                     QString(d->isDownscaled ? "rendering..." : "rendered"),
                                     QString(d->useBucketRender ? "bucket" : "multipass"));
    d->m_painter.drawText(d->m_pixmap.rect(), Qt::AlignBottom | Qt::AlignLeft, legends);

    d->m_painter.restore();
}

void Scatter2dChart::doUpdate()
{
    d->needUpdatePixmap = false;
    d->m_pixmap = QPixmap(size() * devicePixelRatioF() * d->m_pixmapSize);
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
    p.drawPixmap(0, 0, d->m_pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void Scatter2dChart::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // downscale
    drawDownscaled(50);
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
        const int windowUnit = d->m_pixmap.height() / devicePixelRatioF();
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

        d->m_offsetX += delposs.x() * d->m_pixmapSize;
        d->m_offsetY -= delposs.y() * d->m_pixmapSize;

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
        drawDownscaled(10);
        d->needUpdatePixmap = true;
        update();
    }
    // reserved
}

void Scatter2dChart::contextMenuEvent(QContextMenuEvent *event)
{
    const int mouseXPos = event->pos().x();
    const int mouseYPos = height() - event->pos().y();

    const double scaleRatio = d->m_pixmap.height() / devicePixelRatioF();
    const QString sizePos = QString("Cursor: x: %1 | y: %2 | %3\%")
                                .arg(QString::number(((d->m_offsetX - mouseXPos * d->m_pixmapSize) / scaleRatio) / d->m_zoomRatio * -1.0, 'f', 6),
                                     QString::number(((d->m_offsetY - mouseYPos * d->m_pixmapSize) / scaleRatio) / d->m_zoomRatio * -1.0, 'f', 6),
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
    menu.addAction(d->setParticleSize);
    menu.addAction(d->setPixmapSize);
    menu.exec(event->globalPos());
}

void Scatter2dChart::changeZoom()
{
    const double currentZoom = d->m_zoomRatio * 100.0;
    const double scaleRatio = d->m_pixmap.height() / devicePixelRatioF();
    const double currentXOffset = (d->m_offsetX / scaleRatio) / d->m_zoomRatio;
    const double currentYOffset = (d->m_offsetY / scaleRatio) / d->m_zoomRatio;
    bool isZoomOkay(false);
    const double setZoom =
        QInputDialog::getDouble(this, "Set zoom", "Zoom", currentZoom, 80.0, 20000.0, 1, &isZoomOkay);
    if (isZoomOkay) {
        d->m_zoomRatio = setZoom / 100.0;
        d->m_offsetX = (currentXOffset * d->m_zoomRatio) * scaleRatio;
        d->m_offsetY = (currentYOffset * d->m_zoomRatio) * scaleRatio;
        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::changeOrigin()
{
    const double scaleRatio = d->m_pixmap.height() / devicePixelRatioF();
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
        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::copyOrigAndZoom()
{
    const double currentZoom = d->m_zoomRatio * 100.0;
    const double scaleRatio = d->m_pixmap.height() / devicePixelRatioF();
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
        const double scaleRatio = d->m_pixmap.height() / devicePixelRatioF();
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

    drawDownscaled(50);
    d->needUpdatePixmap = true;
    update();
}

void Scatter2dChart::changeAlpha()
{
    const double currentAlpha = d->m_cPoints.at(0).second.alphaF();
    bool isAlphaOkay(false);
    const double setAlpha = QInputDialog::getDouble(this,
                                                    "Set alpha",
                                                    "Per-particle alpha",
                                                    currentAlpha,
                                                    0.1,
                                                    1.0,
                                                    2,
                                                    &isAlphaOkay,
                                                    Qt::WindowFlags(),
                                                    0.1);
    if (isAlphaOkay) {
        for (ColorPoint &p : d->m_cPoints) {
            p.second.setAlphaF(setAlpha);
        }

        drawDownscaled(50);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::changeParticleSize()
{
    const int currentParticleSize = d->m_particleSizeStored;
    bool isParSizeOkay(false);
    const int setParticleSize = QInputDialog::getInt(this,
                                                     "Set particle size",
                                                     "Per-particle size",
                                                     currentParticleSize,
                                                     1,
                                                     10,
                                                     1,
                                                     &isParSizeOkay);
    if (isParSizeOkay) {
        d->m_particleSizeStored = setParticleSize;
        drawDownscaled(50);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::changePixmapSize()
{
    const double currentPixmapSize = d->m_pixmapSize;
    bool isPixSizeOkay(false);
    const double setPixmapSize =
        QInputDialog::getDouble(this, "Set render scaling", "Scale", currentPixmapSize, 1.0, 8.0, 1, &isPixSizeOkay);
    if (isPixSizeOkay) {
        d->m_pixmapSize = setPixmapSize;
        d->m_offsetX = d->m_offsetX * (d->m_pixmapSize / currentPixmapSize);
        d->m_offsetY = d->m_offsetY * (d->m_pixmapSize / currentPixmapSize);

        drawDownscaled(50);
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
        const int iterSize = d->m_neededParticles / adaptiveIterMaxRenderedPoints; // 50k maximum particles
        if (iterSize > 1) {
            d->m_dArrayIterSize = iterSize;
            d->m_particleSize = 4 * d->m_pixmapSize;
        } else {
            d->m_dArrayIterSize = 1;
            d->m_particleSize = 4 * d->m_pixmapSize;
        }
    } else {
        // static downscaling
        if (d->m_cPoints.size() > 2000000) {
            d->m_dArrayIterSize = 100;
        } else if (d->m_cPoints.size() > 500000) {
            d->m_dArrayIterSize = 50;
        } else {
            d->m_dArrayIterSize = 10;
        }
        d->m_particleSize = 4 * d->m_pixmapSize;
    }
}

void Scatter2dChart::resetCamera()
{
    drawDownscaled(50);

    d->m_zoomRatio = 1.1;
    d->m_offsetX = 100;
    d->m_offsetY = 50;

    d->needUpdatePixmap = true;
    update();
}

QPixmap *Scatter2dChart::getFullPixmap()
{
    return &d->m_pixmap;
}
