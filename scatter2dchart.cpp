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
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QColorSpace>
#include <QDebug>
#include <QFileDialog>
#include <QFuture>
#include <QInputDialog>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QTimer>
#include <QVector3D>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

#include <lcms2.h>

#include "constant_dataset.h"
#include "scatter2dchart.h"

// adaptive downsampling params, hardcoded
static const int adaptiveIterMaxPixels = 50000;
static const int adaptiveIterMaxRenderedPoints = 25000;

// Hard limit before switching to bucket rendering
static const int maxPixelsBeforeBucket = 4000000;

/*
 * Data point rendering modes:
 * - Bucket, mostly slower but lighter in RAM
 * - Progressive, mostly faster but RAM heavy especially with large upscaling
 */
static const int bucketDefaultSize = 256;
static const int bucketDownscaledSize = 1024;

class Q_DECL_HIDDEN Scatter2dChart::Private
{
public:
    bool needUpdatePixmap{false};
    bool isMouseHold{false};
    bool isDownscaled{false};
    bool useBucketRender{false};
    bool keepCentered{false};
    bool renderSlices{false};
    bool inputScatterData{true};
    bool finishedRender{false};
    bool isCancelFired{false};
    bool isSettingOverride{false};
    QPainter m_painter;
    QImage m_pixmap;
    QImage m_ScatterPixmap;
    QImage m_ScatterTempPixmap;
    QVector<ImageXYZDouble> m_dOutGamut;
    QVector<QVector2D> m_blackbodyLocus;
    QVector<QVector2D> m_daylightLocus;
    QVector3D m_dWhitePoint;
    int m_particleSize{0};
    int m_particleSizeStored{0};
    int m_drawnParticles{0};
    int m_lastDrawnParticles{0};
    int m_neededParticles{0};
    int m_lastNeededParticles{0};
    int adaptiveIterVal{0};
    int m_numberOfSlices{0};
    int m_slicePos{0};
    int m_scatterIndex{0}; // unused?
    qint64 m_msecRenderTime{0};
    double m_pointOpacity{1.0};
    double m_minY{0.0};
    double m_maxY{1.0};
    double m_zoomRatio{1.1};
    double m_pixmapSize{1.0};
    double m_offsetX{100};
    double m_offsetY{50};
    double m_mpxPerSec{0};
    QPoint m_lastPos{};
    QPointF m_lastCenter{};
    int m_dArrayIterSize = 1;
    QTimer *m_scrollTimer;
    QElapsedTimer m_renderTimer;
    QFont m_labelFont;
    QColor m_bgColor;

    QColorSpace m_scProfile;
    QColorSpace m_imageSpace;
    QImage::Format m_imageFormat;

    QMutex m_locker;

    QVector<ColorPoint> *m_cPoints;
    QVector<cmsCIExyY> m_adaptedColorChecker76;
    QVector<cmsCIExyY> m_adaptedColorChecker;
    QVector<cmsCIExyY> m_adaptedColorCheckerNew;
    int m_idealThrCount = 0;

    QAction *setZoom;
    QAction *setOrigin;
    QAction *copyOrigAndZoom;
    QAction *pasteOrigAndZoom;
    QAction *drawLabels;
    QAction *drawGrids;
    QAction *drawSrgbGamut;
    QAction *drawImgGamut;
    QAction *drawMacAdamEllipses;
    QAction *drawColorCheckerPoints;
    QAction *drawColorCheckerPointsOld;
    QAction *drawColorCheckerPoints76;
    QAction *drawBlackbodyLocus;
    QAction *setStaticDownscale;
    QAction *setAlpha;
    QAction *setAntiAliasing;
    QAction *setParticleSize;
    QAction *setPixmapSize;
    QAction *setBgColor;
    QAction *saveSlicesAsImage;
    QAction *drawStats;
    QAction *use16Bit;

    QFutureWatcher<QPair<QImage, QRect>> m_future;

    bool enableLabels{true};
    bool enableGrids{true};
    bool enableSrgbGamut{true};
    bool enableImgGamut{true};
    bool enableMacAdamEllipses{false};
    bool enableColorCheckerPoints{false};
    bool enableColorCheckerPointsOld{false};
    bool enableColorCheckerPoints76{false};
    bool enableBlackbodyLocus{false};
    bool enableStaticDownscale{false};
    bool enableAA{false};
    bool enableStats{false};
    bool enable16Bit{false};

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

    // reserved soon
    d->m_scProfile = QColorSpace::SRgb;
    d->m_imageSpace = QColorSpace::SRgb;
    d->m_imageFormat = QImage::Format_ARGB32;

    d->m_labelFont = QFont("Courier New", 11, QFont::Medium);
    d->m_bgColor = Qt::black;

    d->m_clipb = QApplication::clipboard();

    d->setZoom = new QAction("Set zoom...");
    connect(d->setZoom, &QAction::triggered, this, &Scatter2dChart::changeZoom);

    d->setOrigin = new QAction("Set center...");
    connect(d->setOrigin, &QAction::triggered, this, &Scatter2dChart::changeOrigin);

    d->copyOrigAndZoom = new QAction("Copy plot state");
    connect(d->copyOrigAndZoom, &QAction::triggered, this, &Scatter2dChart::copyOrigAndZoom);

    d->pasteOrigAndZoom = new QAction("Paste plot state");
    connect(d->pasteOrigAndZoom, &QAction::triggered, this, &Scatter2dChart::pasteOrigAndZoom);

    d->drawLabels = new QAction("Statistics");
    d->drawLabels->setCheckable(true);
    d->drawLabels->setChecked(d->enableLabels);
    connect(d->drawLabels, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawGrids = new QAction("Grids and spectral line");
    d->drawGrids->setCheckable(true);
    d->drawGrids->setChecked(d->enableGrids);
    connect(d->drawGrids, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawSrgbGamut = new QAction("sRGB gamut");
    d->drawSrgbGamut->setCheckable(true);
    d->drawSrgbGamut->setChecked(d->enableSrgbGamut);
    connect(d->drawSrgbGamut, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawImgGamut = new QAction("Image gamut");
    d->drawImgGamut->setCheckable(true);
    d->drawImgGamut->setChecked(d->enableImgGamut);
    connect(d->drawImgGamut, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawMacAdamEllipses = new QAction("MacAdam ellipses");
    d->drawMacAdamEllipses->setToolTip("Draw in both normal, and 10x size as depicted in MacAdam's paper.");
    d->drawMacAdamEllipses->setCheckable(true);
    d->drawMacAdamEllipses->setChecked(d->enableMacAdamEllipses);
    connect(d->drawMacAdamEllipses, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawColorCheckerPoints = new QAction("ColorChecker (After Nov 2014)");
    d->drawColorCheckerPoints->setCheckable(true);
    d->drawColorCheckerPoints->setChecked(d->enableColorCheckerPoints);
    connect(d->drawColorCheckerPoints, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawColorCheckerPointsOld = new QAction("ColorChecker (Before Nov 2014)");
    d->drawColorCheckerPointsOld->setCheckable(true);
    d->drawColorCheckerPointsOld->setChecked(d->enableColorCheckerPointsOld);
    connect(d->drawColorCheckerPointsOld, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawColorCheckerPoints76 = new QAction("ColorChecker (Classic 1976)");
    d->drawColorCheckerPoints76->setCheckable(true);
    d->drawColorCheckerPoints76->setChecked(d->enableColorCheckerPoints76);
    connect(d->drawColorCheckerPoints76, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->drawBlackbodyLocus = new QAction("Blackbody and Daylight locus");
    d->drawBlackbodyLocus->setCheckable(true);
    d->drawBlackbodyLocus->setChecked(d->enableBlackbodyLocus);
    connect(d->drawBlackbodyLocus, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->setStaticDownscale = new QAction("Disable dynamic panning");
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

    d->setBgColor = new QAction("Set background color...");
    connect(d->setBgColor, &QAction::triggered, this, &Scatter2dChart::changeBgColor);

    d->saveSlicesAsImage = new QAction("Save Y slices as image...");
    connect(d->saveSlicesAsImage, &QAction::triggered, this, &Scatter2dChart::saveSlicesAsImage);

    d->drawStats = new QAction("Show extras on statistics");
    d->drawStats->setCheckable(true);
    d->drawStats->setChecked(d->enableStats);
    connect(d->drawStats, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    d->use16Bit = new QAction("16 bit per channel");
    d->use16Bit->setCheckable(true);
    d->use16Bit->setChecked(d->enable16Bit);
    connect(d->use16Bit, &QAction::triggered, this, &Scatter2dChart::changeProperties);

    if (QThread::idealThreadCount() > 1) {
        d->m_idealThrCount = QThread::idealThreadCount();
    } else {
        d->m_idealThrCount = 1;
    }

    /*
     *  Planckian locus approximation
     *
     *  References:
     *  Kang, B., Moon, O., Hong, C., Lee, H., Cho, B., & Kim, Y. (2002). Design of advanced color: Temperature control
     * system for HDTV applications. Journal of the Korean Physical Society, 41(6), 865-871.
     */
    for (int i = 1700; i <= 25000; i += 50) {
        const double temp = static_cast<float>(i);
        double x;
        double y;

        if (i <= 4000) {
            x = (-0.2661239 * (1.0e+9 / std::pow(temp, 3.0))) - (0.2343589 * (1.0e+6 / std::pow(temp, 2.0))) + (0.8776956 * (1.0e+3 / temp)) + 0.179910;
        } else {
            x = (-3.0258469 * (1.0e+9 / std::pow(temp, 3.0))) + (2.1070379 * (1.0e+6 / std::pow(temp, 2.0))) + (0.2226347 * (1.0e+3 / temp)) + 0.240390;
        }

        if (i <= 2222) {
            y = (-1.1063814 * std::pow(x, 3.0)) - (1.34811020 * std::pow(x, 2.0)) + (2.18555832 * x) - 0.20219683;
        } else if (i <= 4000) {
            y = (-0.9549476 * std::pow(x, 3.0)) - (1.37418593 * std::pow(x, 2.0)) + (2.09137015 * x) - 0.16748867;
        } else {
            y = (3.0817580 * std::pow(x, 3.0)) - (5.87338670 * std::pow(x, 2.0)) + (3.75112997 * x) - 0.37001483;
        }

        d->m_blackbodyLocus.append({static_cast<float>(x), static_cast<float>(y)});
    }

    for (int i = 4000; i <= 10000; i += 50) {
        const double temp = static_cast<float>(i);
        double x;
        double y;

        if (i <= 7000) {
            x = 0.244063 + (0.09911 * (1.0e+3 / temp)) + (2.9678 * (1.0e+6 / std::pow(temp, 2.0))) - (4.6070 * (1.0e+9 / std::pow(temp, 3.0)));
        } else {
            x = 0.237040 + (0.24748 * (1.0e+3 / temp)) + (1.9018 * (1.0e+6 / std::pow(temp, 2.0))) - (2.0064 * (1.0e+9 / std::pow(temp, 3.0)));
        }

        y = (-3.000 * std::pow(x, 2.0)) + (2.870 * x) - 0.275;

        d->m_daylightLocus.append({static_cast<float>(x), static_cast<float>(y)});
    }

    connect(&d->m_future, &QFutureWatcher<void>::resultReadyAt, this, &Scatter2dChart::drawFutureAt);
    connect(&d->m_future, &QFutureWatcher<void>::finished, this, &Scatter2dChart::onFinishedDrawing);
}

Scatter2dChart::~Scatter2dChart()
{
    cancelRender();
    delete d;
}

void Scatter2dChart::overrideSettings(PlotSetting2D &plot)
{
    d->enableAA = plot.enableAA;
    d->enableStaticDownscale = plot.disableDynPanning;
    d->enable16Bit = plot.use16Bit;

    d->enableLabels = plot.showStatistics;
    d->enableGrids = plot.showGridsAndSpectrum;
    d->enableSrgbGamut = plot.showsRGBGamut;
    d->enableImgGamut = plot.showImageGamut;
    d->enableMacAdamEllipses = plot.showMacAdamEllipses;
    d->enableColorCheckerPoints = plot.showColorCheckerPoints;
    d->enableBlackbodyLocus = plot.showBlBodyLocus;

    d->m_pointOpacity = plot.particleOpacity;
    d->m_particleSize = plot.particleSize;
    d->m_particleSizeStored = plot.particleSize;
    d->m_pixmapSize = plot.renderScale;

    d->drawLabels->setChecked(d->enableLabels);
    d->drawGrids->setChecked(d->enableGrids);
    d->drawSrgbGamut->setChecked(d->enableSrgbGamut);
    d->drawImgGamut->setChecked(d->enableImgGamut);
    d->drawMacAdamEllipses->setChecked(d->enableMacAdamEllipses);
    d->drawColorCheckerPoints->setChecked(d->enableColorCheckerPoints);
    d->drawBlackbodyLocus->setChecked(d->enableBlackbodyLocus);
    d->setStaticDownscale->setChecked(d->enableStaticDownscale);
    d->setAntiAliasing->setChecked(d->enableAA);
    d->use16Bit->setChecked(d->enable16Bit);

    if (d->enable16Bit) {
        d->m_imageFormat = QImage::Format_RGBA64;
    }

    d->isSettingOverride = true;
}

void Scatter2dChart::addDataPoints(QVector<ColorPoint> &dArray, int size)
{
    d->needUpdatePixmap = true;
    d->m_neededParticles = dArray.size();

    // set alpha based on numpoints
    const double alphaToLerp =
        std::min(std::max(1.0 - (dArray.size() - 50000.0) / (5000000.0 - 50000.0), 0.0), 1.0);
    const double alphaLerpToGamma = 0.1 + ((1.0 - 0.1) * std::pow(alphaToLerp, 5.5));

    d->m_cPoints = &dArray;

    std::vector<double> minMaxY;
    for (int i = 0; i < d->m_cPoints->size(); i++) {
        minMaxY.emplace_back(d->m_cPoints->at(i).first.Z);
    }
    const double min = *std::min_element(minMaxY.cbegin(), minMaxY.cend());
    const double max = *std::max_element(minMaxY.cbegin(), minMaxY.cend());
    d->m_minY = min;
    d->m_maxY = max;

    if (d->m_cPoints->size() > adaptiveIterMaxPixels) {
        d->adaptiveIterVal = d->m_cPoints->size() / adaptiveIterMaxPixels;
    } else {
        d->adaptiveIterVal = 1;
    }

    if (!d->isSettingOverride) {
        d->m_particleSize = size;
        d->m_particleSizeStored = size;
        d->m_pointOpacity = alphaLerpToGamma;
    }
}

void Scatter2dChart::addGamutOutline(QVector<ImageXYZDouble> &dOutGamut, QVector3D &dWhitePoint)
{
    d->m_dOutGamut = dOutGamut;
    d->m_dWhitePoint = dWhitePoint;

    const cmsCIExyY prfWPxyY{d->m_dWhitePoint.x(), d->m_dWhitePoint.y(), d->m_dWhitePoint.z()};
    const cmsCIExyY ccWPxyY76{Macbeth_chart_1976[18][0], Macbeth_chart_1976[18][1], Macbeth_chart_1976[18][2]};

    d->m_adaptedColorChecker.clear();

    cmsCIEXYZ prfWPXYZ;
    cmsCIEXYZ ccWPXYZ76;

    cmsxyY2XYZ(&prfWPXYZ, &prfWPxyY);
    cmsxyY2XYZ(&ccWPXYZ76, &ccWPxyY76);

    // calculate ColorChecker points to adapted illuminant
    for (int i = 0; i < 24; i++) {
        // 1976
        const cmsCIExyY srcxyY76{Macbeth_chart_1976[i][0], Macbeth_chart_1976[i][1], Macbeth_chart_1976[i][2]};
        cmsCIEXYZ srcXYZ76;
        cmsCIEXYZ destXYZ76;
        cmsCIExyY destxyY76;

        cmsxyY2XYZ(&srcXYZ76, &srcxyY76);
        cmsAdaptToIlluminant(&destXYZ76, &ccWPXYZ76, &prfWPXYZ, &srcXYZ76);

        cmsXYZ2xyY(&destxyY76, &destXYZ76);
        d->m_adaptedColorChecker76.append({destxyY76.x, destxyY76.y, destxyY76.Y});

        // Pre-Nov2014
        const cmsCIExyY srcxyY{Macbeth_chart_2005[i][0], Macbeth_chart_2005[i][1], Macbeth_chart_2005[i][2]};
        cmsCIEXYZ srcXYZ;
        cmsCIEXYZ destXYZ;
        cmsCIExyY destxyY;

        cmsxyY2XYZ(&srcXYZ, &srcxyY);
        cmsAdaptToIlluminant(&destXYZ, cmsD50_XYZ(), &prfWPXYZ, &srcXYZ);

        cmsXYZ2xyY(&destxyY, &destXYZ);
        d->m_adaptedColorChecker.append({destxyY.x, destxyY.y, destxyY.Y});

        // Post-Nov2014
        const cmsCIELab srcNewLab{ColorChecker_After_Nov2014_Lab[i][0], ColorChecker_After_Nov2014_Lab[i][1], ColorChecker_After_Nov2014_Lab[i][2]};
        cmsCIEXYZ srcXYZNew;
        cmsCIEXYZ dstXYZNew;
        cmsCIExyY dstxyYNew;

        cmsLab2XYZ(cmsD50_XYZ(), &srcXYZNew, &srcNewLab);
        cmsAdaptToIlluminant(&dstXYZNew, cmsD50_XYZ(), &prfWPXYZ, &srcXYZNew);
        cmsXYZ2xyY(&dstxyYNew, &dstXYZNew);

        d->m_adaptedColorCheckerNew.append({dstxyYNew.x, dstxyYNew.y, dstxyYNew.Y});
    }
}

void Scatter2dChart::addColorSpace(QByteArray &rawICCProfile)
{
    // reserved
    // d->m_scProfile = QColorSpace::fromIccProfile(rawICCProfile);
}

inline QPointF Scatter2dChart::mapPoint(QPointF xy) const
{
    // Maintain ascpect ratio, otherwise use width() on X
    return QPointF(((xy.x() * d->m_zoomRatio) * d->m_pixmap.height() + d->m_offsetX),
                   ((d->m_pixmap.height() - ((xy.y() * d->m_zoomRatio) * d->m_pixmap.height())) - d->m_offsetY));
}

inline QPointF Scatter2dChart::mapScreenPoint(QPointF xy) const
{
    return QPointF(((d->m_offsetX - (xy.x() / devicePixelRatioF()) * d->m_pixmapSize) / (d->m_pixmap.height() * 1.0)) / d->m_zoomRatio * -1.0,
                   ((d->m_offsetY - ((height() / devicePixelRatioF()) - (xy.y() / devicePixelRatioF())) * d->m_pixmapSize) / (d->m_pixmap.height() * 1.0)) / d->m_zoomRatio * -1.0);
}

inline double Scatter2dChart::oneUnitInPx() const
{
    return (static_cast<double>(d->m_pixmap.height()) * d->m_zoomRatio);
}

Scatter2dChart::RenderBounds Scatter2dChart::getRenderBounds() const
{
    const double scaleH = d->m_pixmap.height();
    const double scaleW = d->m_pixmap.width();
    const double originX = (d->m_offsetX / scaleH) / d->m_zoomRatio * -1.0;
    const double originY = (d->m_offsetY / scaleH) / d->m_zoomRatio * -1.0;
    const double maxX = ((d->m_offsetX - scaleW) / scaleH) / d->m_zoomRatio * -1.0;
    const double maxY = ((d->m_offsetY - scaleH) / scaleH) / d->m_zoomRatio * -1.0;

    return RenderBounds({originX, originY, maxX, maxY});
}

void Scatter2dChart::drawDataPoints()
{
    // TODO: kinda spaghetti here...

    // internal function for painting the chunks concurrently
    std::function<QPair<QImage, QRect>(const QPair<QVector<ColorPoint *>, QRect> &)> const paintInChunk =
        [&](const QPair<QVector<ColorPoint *>, QRect> &chunk) -> QPair<QImage, QRect> {
        if (chunk.first.size() == 0) {
            return {QImage(), QRect()};
        }
        const QSize workerDim = [&]() {
            if (d->useBucketRender) {
                return QSize(bucketDefaultSize, bucketDefaultSize);
            }
            return d->m_pixmap.size();
        }();

        QImage tempMap(workerDim, d->m_imageFormat);
        tempMap.setColorSpace(d->m_scProfile);
        tempMap.fill(Qt::transparent);
        QPainter tempPainterMap;

        if (!tempPainterMap.begin(&tempMap)) {
            return {QImage(), QRect()};
        }

        if (!d->isDownscaled && d->enableAA) {
            tempPainterMap.setRenderHint(QPainter::Antialiasing);
        }
        tempPainterMap.setPen(Qt::NoPen);
        tempPainterMap.setCompositionMode(QPainter::CompositionMode_Lighten);

        const QPoint offset = [&]() {
            if (!chunk.second.isNull()) {
                return chunk.second.topLeft();
            }
            return QPoint();
        }();

        for (int i = 0; i < chunk.first.size(); i++) {
            if (d->isCancelFired) {
                break;
            }
            const QPointF mapped = [&]() {
                if (offset.isNull()) {
                    return mapPoint(QPointF(chunk.first.at(i)->first.X, chunk.first.at(i)->first.Y));
                }
                return mapPoint(QPointF(chunk.first.at(i)->first.X, chunk.first.at(i)->first.Y)) - offset;
            }();

            const QColor col = [&]() {
                // How do you initiate QColor directly to extended rgb...
                QColor temp;
                QColor temp2 = temp.toExtendedRgb();
                if (d->isDownscaled) {
                    temp2.setRgbF(chunk.first.at(i)->second.R, chunk.first.at(i)->second.G, chunk.first.at(i)->second.B, 0.5);
                    return temp2;
                }
                temp2.setRgbF(chunk.first.at(i)->second.R, chunk.first.at(i)->second.G, chunk.first.at(i)->second.B, d->m_pointOpacity);
                return temp2;
            }();

            tempPainterMap.setBrush(col);

            if (d->enableAA && !d->isDownscaled) {
                tempPainterMap.drawEllipse(mapped, d->m_particleSize / 2.0, d->m_particleSize / 2.0);
            } else {
                tempPainterMap.drawEllipse(mapped.toPoint(), d->m_particleSize / 2, d->m_particleSize / 2);
            }
        }

        tempPainterMap.end();

        if (d->m_scProfile != d->m_imageSpace) {
            tempMap.convertToColorSpace(d->m_imageSpace);
        }

        return {tempMap, chunk.second};
    }; // paintInChunk

    // prepare window dimension
    const RenderBounds rb = getRenderBounds();
    const int pixmapH = d->m_pixmap.height();
    const int pixmapW = d->m_pixmap.width();

    const int pixmapPixSize = pixmapH * pixmapW;

    if (pixmapPixSize > maxPixelsBeforeBucket && !d->isDownscaled) {
        d->useBucketRender = true;
    } else {
        d->useBucketRender = false;
    }

    const int bucketSize = (d->isDownscaled ? bucketDownscaledSize : bucketDefaultSize);
    const int bucketPadding = d->m_particleSize;

    QVector<QPair<QVector<ColorPoint*>, QRect>> fragmentedColPoints;
    QVector<ColorPoint*> temporaryColPoints;

    const bool needUpdate = (d->isDownscaled || d->inputScatterData || d->renderSlices);

    d->m_drawnParticles = 0;
    // calculate an estimate how much points is needed for onscreen rendering
    if (needUpdate) {
        d->m_neededParticles = 0;
        for (int i = 0; i < d->m_cPoints->size(); i += d->adaptiveIterVal) {
            if ((d->m_cPoints->at(i).first.X > rb.originX && d->m_cPoints->at(i).first.X < rb.maxX)
                && (d->m_cPoints->at(i).first.Y > rb.originY && d->m_cPoints->at(i).first.Y < rb.maxY)) {
                d->m_neededParticles++;
            }
        }
        d->m_neededParticles = d->m_neededParticles * d->adaptiveIterVal;
        d->m_lastNeededParticles = d->m_neededParticles;
    } else {
        d->m_neededParticles = d->m_lastNeededParticles;
    }

    // set iteration size (for downscaling)
    if (d->isDownscaled) {
        if (d->enableStaticDownscale) {
            // static downscaling
            if (d->m_cPoints->size() > 2000000) {
                d->m_dArrayIterSize = 100;
            } else if (d->m_cPoints->size() > 500000) {
                d->m_dArrayIterSize = 50;
            } else {
                d->m_dArrayIterSize = 10;
            }
        } else {
            // dynamic downscaling
            const int iter = d->m_neededParticles / adaptiveIterMaxRenderedPoints;
            if (iter > 5) {
                d->m_dArrayIterSize = iter;
            } else {
                d->m_dArrayIterSize = 5;
            }
        }
        d->m_particleSize = 4 * d->m_pixmapSize;
    } else {
        d->m_dArrayIterSize = 1;
    }

    // scoop actual points into render queue
    if (needUpdate) {
        // progressive param
        const int thrCount = (d->isDownscaled ? 2 : d->m_idealThrCount);
        const int chunkSize = d->m_neededParticles / thrCount;

        // bucket param
        const int bucketWNum = std::ceil(pixmapW / (bucketSize * 1.0));
        const int bucketHNum = std::ceil(pixmapH / (bucketSize * 1.0));
        const int bucketTotalNum = bucketWNum * bucketHNum;

        if (d->useBucketRender) {
            fragmentedColPoints.resize(bucketTotalNum);
        }

        // divide to chunks
        for (int i = 0; i < d->m_cPoints->size(); i += d->m_dArrayIterSize) {
            if (d->renderSlices) {
                const double sliceRange = d->m_maxY - d->m_minY;
                const double sliceHalfSize = sliceRange / d->m_numberOfSlices / 2.0;
                const double currentPos =  d->m_minY + ((d->m_slicePos * 1.0 / d->m_numberOfSlices * 1.0) * sliceRange);
                const double minY = currentPos - sliceHalfSize;
                const double maxY = currentPos + sliceHalfSize;
                if (d->m_cPoints->at(i).first.Z < minY || d->m_cPoints->at(i).first.Z > maxY) {
                    continue;
                }
            }
            if (d->useBucketRender) {
                const QPointF map = mapPoint(QPointF(d->m_cPoints->at(i).first.X, d->m_cPoints->at(i).first.Y));
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
                            && (map.x() > 0 && map.x() < pixmapW) && (map.y() > 0 && map.y() < pixmapH)) {
                            isDataWritten = true;
//                            const QPoint origin(orX, orY);
                            const QRect bucket(orX, orY, bucketSize, bucketSize);
                            fragmentedColPoints[bucketAbsPos].second = bucket;
                            fragmentedColPoints[bucketAbsPos].first.append(
                                const_cast<ColorPoint *>(&d->m_cPoints->at(i)));
                        }
                    }
                }
                // make sure padding data isn't counted
                if (isDataWritten) {
                    d->m_drawnParticles++;
                }
            } else {
                // mutipass
                if ((d->m_cPoints->at(i).first.X > rb.originX && d->m_cPoints->at(i).first.X < rb.maxX)
                    && (d->m_cPoints->at(i).first.Y > rb.originY && d->m_cPoints->at(i).first.Y < rb.maxY)) {
                    const QPointF map = mapPoint(QPointF(d->m_cPoints->at(i).first.X, d->m_cPoints->at(i).first.Y));
                    temporaryColPoints.append(const_cast<ColorPoint *>(&d->m_cPoints->at(i)));
                    d->m_drawnParticles++;
                }

                // add passes when a chunk is filled
                if (chunkSize > 0 && temporaryColPoints.size() == chunkSize && !d->isDownscaled) {
                    fragmentedColPoints.append({temporaryColPoints, d->m_pixmap.rect()});
                    temporaryColPoints.clear();
                }
            }
        }

        // pick leftover (last) progressive pass
        if (!d->useBucketRender && temporaryColPoints.size() > 0) {
            fragmentedColPoints.append({temporaryColPoints, d->m_pixmap.rect()});
            temporaryColPoints.clear();
        }
    }

    if ((!d->isDownscaled && d->inputScatterData) || d->renderSlices) {
        d->inputScatterData = false;
        d->m_future.setFuture(QtConcurrent::mapped(fragmentedColPoints, paintInChunk));
        d->m_lastDrawnParticles = d->m_drawnParticles;
        d->m_renderTimer.start();
    }

    d->m_painter.save();

    if (d->renderSlices) {
        d->m_future.waitForFinished();
        for (auto it = d->m_future.future().constBegin(); it != d->m_future.future().constEnd(); it++) {
            if (!it->first.isNull()) {
                d->m_painter.drawImage(it->second, it->first);
            }
        }
        d->m_painter.restore();
        return;
    }

    if (d->isDownscaled) {
        if (!fragmentedColPoints.isEmpty()) {
            const auto out = paintInChunk(fragmentedColPoints.at(0));
            d->m_ScatterTempPixmap = out.first;
            d->m_painter.drawImage(d->m_pixmap.rect(), out.first);
        }
        d->finishedRender = false;
        d->m_lastDrawnParticles = d->m_drawnParticles;
    } else {
        if (!d->m_ScatterTempPixmap.isNull() && !d->finishedRender) {
            if (d->useBucketRender) {
                d->m_painter.setOpacity(0.50);
            } else {
                d->m_painter.setOpacity(0.75);
            }
            d->m_painter.drawImage(d->m_pixmap.rect(), d->m_ScatterTempPixmap);
            d->m_painter.setOpacity(1);
            d->m_painter.setCompositionMode(QPainter::CompositionMode_Lighten);
        }

        d->m_painter.drawImage(d->m_pixmap.rect(), d->m_ScatterPixmap);
    }

    d->m_painter.restore();
}

void Scatter2dChart::drawSpectralLine()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_Difference);
    QPen pn;

    pn.setColor(QColor(64, 64, 64));
    pn.setWidth(1);
    d->m_painter.setPen(pn);
    d->m_painter.setBrush(Qt::transparent);

    d->m_painter.setFont(QFont("Courier New", 8.0));

    QPainterPath spectral(mapPoint(QPointF(spectral_chromaticity[0][0], spectral_chromaticity[0][1])));
    for (int i = 1; i < 81; i++) {
        const QPointF specPoint = mapPoint(QPointF(spectral_chromaticity[i][0], spectral_chromaticity[i][1]));
        spectral.lineTo(specPoint);
    }
    spectral.closeSubpath();

    d->m_painter.drawPath(spectral);

    d->m_painter.setRenderHint(QPainter::Antialiasing, false);

    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    d->m_painter.setBrush(Qt::black);

    const int labelIter = [&]() {
        const double ratio = d->m_zoomRatio * d->m_pixmapSize;
        if (ratio < 1.5) {
            return 4;
        } else if (ratio < 3.0) {
            return 2;
        }
        return 1;
    }();

    for (int x = 385; x < 700; x += 5) {
        int i = (x - 380) / 5;
        if (i % labelIter == 0) {
            const QPointF specPoint = mapPoint(QPointF(spectral_chromaticity[i][0], spectral_chromaticity[i][1]));

            d->m_painter.translate(specPoint);

            QRect bound;
            d->m_painter.setPen(Qt::gray);
            d->m_painter.drawText(QRect(-25,-25,50,50), Qt::AlignCenter, QString::number(x), &bound);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(bound);
            d->m_painter.setPen(Qt::gray);
            d->m_painter.drawText(QRect(-25,-25,50,50), Qt::AlignCenter, QString::number(x));

            d->m_painter.resetTransform();
        }
    }

    d->m_painter.restore();
}

void Scatter2dChart::drawSrgbTriangle()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    if (d->m_bgColor == Qt::black) {
        d->m_painter.setCompositionMode(QPainter::CompositionMode_Plus);
    } else {
        d->m_painter.setCompositionMode(QPainter::CompositionMode_Difference);
    }
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
    if (d->m_bgColor == Qt::black) {
        d->m_painter.setCompositionMode(QPainter::CompositionMode_Plus);
    } else {
        d->m_painter.setCompositionMode(QPainter::CompositionMode_Difference);
    }

    QPen pn;
    pn.setColor(QColor(128, 0, 0, 128));
    pn.setWidth(2);
    d->m_painter.setPen(pn);

    d->m_painter.setBrush(Qt::transparent);

    const int pointSize = 3;

    QPolygonF gamutPoly;

    for (int i = 0; i < d->m_dOutGamut.size(); i++) {
        gamutPoly << mapPoint(QPointF(d->m_dOutGamut.at(i).X, d->m_dOutGamut.at(i).Y));
    }

    d->m_painter.drawPolygon(gamutPoly);

    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QPointF mapW = mapPoint(QPointF(d->m_dWhitePoint.x(), d->m_dWhitePoint.y()));
    d->m_painter.setBrush(Qt::white);
    d->m_painter.drawEllipse(mapW.x() - (4 / 2), mapW.y() - (4 / 2), 4, 4);

    d->m_painter.restore();
}

void Scatter2dChart::drawMacAdamEllipses()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_Difference);

    QPen pn;
    pn.setColor(QColor(128, 128, 128, 128));
    pn.setWidth(2);
    pn.setStyle(Qt::DotLine);

    QPen pnInner;
    pnInner.setColor(QColor(200, 200, 200, 128));
    pnInner.setWidth(2);

    d->m_painter.setBrush(Qt::transparent);

    for (int i = 0; i < 25; i++) {
        const QPointF centerCol = mapPoint({MacAdam_ellipses[i][0], MacAdam_ellipses[i][1]});
        const QSizeF ellipseSize(MacAdam_ellipses[i][5], MacAdam_ellipses[i][6]);
        const double theta(MacAdam_ellipses[i][7]);

        d->m_painter.resetTransform();
        d->m_painter.translate(centerCol);
        d->m_painter.rotate(theta * -1.0);

        d->m_painter.setPen(pn);
        d->m_painter.drawEllipse(QPointF(0, 0),
                                 ellipseSize.width() * oneUnitInPx() / 100.0,
                                 ellipseSize.height() * oneUnitInPx() / 100.0);

        d->m_painter.setPen(pnInner);
        d->m_painter.drawEllipse(QPointF(0, 0),
                                 ellipseSize.width() * oneUnitInPx() / 1000.0,
                                 ellipseSize.height() * oneUnitInPx() / 1000.0);
    }
    d->m_painter.resetTransform();

    d->m_painter.restore();
}

void Scatter2dChart::drawColorCheckerPoints()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPen pn;
    pn.setColor(Qt::yellow);
    pn.setCapStyle(Qt::RoundCap);
    pn.setWidthF(1.0);

    QPen pnOuter;
    pnOuter.setColor(Qt::black);
    pn.setCapStyle(Qt::RoundCap);
    pnOuter.setWidthF(2.0);

    d->m_painter.setBrush(QColor(0, 0, 0, 200));
    d->m_painter.setPen(pn);

    QVector<QLineF> cross = {{-10.0, 0.0, 10.0, 0.0}, {0.0, -10.0, 0.0, 10.0}};

    d->m_painter.setFont(QFont("Courier New", 12.0, QFont::Bold));

    const double ratio = d->m_zoomRatio * d->m_pixmapSize;

    for (int i = 0; i < 24; i++) {
        const QPointF centerCol = mapPoint({d->m_adaptedColorChecker.at(i).x, d->m_adaptedColorChecker.at(i).y});

        d->m_painter.resetTransform();
        d->m_painter.translate(centerCol);

        d->m_painter.setPen(pnOuter);
        d->m_painter.drawLines(cross);

        d->m_painter.setPen(pn);
        d->m_painter.drawLines(cross);

        if (i < 18 || ratio >= 10.0) {
            QRect boundRect;
            d->m_painter.setPen(Qt::yellow);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, QString::number(i+1), &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::yellow);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, QString::number(i+1));
        } else if (i == 18 && ratio < 10.0) {
            QRect boundRect;
            d->m_painter.setPen(Qt::yellow);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, "19-24", &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::yellow);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, "19-24");
        }
    }
    d->m_painter.resetTransform();

    d->m_painter.restore();
}

void Scatter2dChart::drawColorCheckerPointsNew()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPen pn;
    pn.setColor(Qt::white);
    pn.setCapStyle(Qt::RoundCap);
    pn.setWidthF(1.0);

    QPen pnOuter;
    pnOuter.setColor(Qt::black);
    pn.setCapStyle(Qt::RoundCap);
    pnOuter.setWidthF(2.0);

    d->m_painter.setBrush(QColor(0, 0, 0, 200));
    d->m_painter.setPen(pn);

    QVector<QLineF> cross = {{-10.0, 0.0, 10.0, 0.0}, {0.0, -10.0, 0.0, 10.0}};

    d->m_painter.setFont(QFont("Courier New", 12.0, QFont::Bold));

    const double ratio = d->m_zoomRatio * d->m_pixmapSize;

    for (int i = 0; i < 24; i++) {
        const QPointF centerCol = mapPoint({d->m_adaptedColorCheckerNew.at(i).x, d->m_adaptedColorCheckerNew.at(i).y});

        d->m_painter.resetTransform();
        d->m_painter.translate(centerCol);

        d->m_painter.setPen(pnOuter);
        d->m_painter.drawLines(cross);

        d->m_painter.setPen(pn);
        d->m_painter.drawLines(cross);

        if (i < 18 || ratio >= 10.0) {
            QRect boundRect;
            d->m_painter.setPen(Qt::white);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, QString::number(i+1), &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::white);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, QString::number(i+1));
        } else if (i == 18 && ratio < 10.0) {
            QRect boundRect;
            d->m_painter.setPen(Qt::white);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, "19-24", &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::white);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, "19-24");
        }
    }
    d->m_painter.resetTransform();

    d->m_painter.restore();
}

void Scatter2dChart::drawColorCheckerPoints76()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPen pn;
    pn.setColor(Qt::cyan);
    pn.setCapStyle(Qt::RoundCap);
    pn.setWidthF(1.0);

    QPen pnOuter;
    pnOuter.setColor(Qt::black);
    pn.setCapStyle(Qt::RoundCap);
    pnOuter.setWidthF(2.0);

    d->m_painter.setBrush(QColor(0, 0, 0, 200));
    d->m_painter.setPen(pn);

    QVector<QLineF> cross = {{-10.0, 0.0, 10.0, 0.0}, {0.0, -10.0, 0.0, 10.0}};

    d->m_painter.setFont(QFont("Courier New", 12.0, QFont::Bold));

    for (int i = 0; i < 24; i++) {
        const QPointF centerCol = mapPoint({d->m_adaptedColorChecker76.at(i).x, d->m_adaptedColorChecker76.at(i).y});

        d->m_painter.resetTransform();
        d->m_painter.translate(centerCol);

        d->m_painter.setPen(pnOuter);
        d->m_painter.drawLines(cross);

        d->m_painter.setPen(pn);
        d->m_painter.drawLines(cross);

        if (i < 18) {
            QRect boundRect;
            d->m_painter.setPen(Qt::cyan);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, QString::number(i+1), &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::cyan);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, QString::number(i+1));
        } else if (i == 18) {
            QRect boundRect;
            d->m_painter.setPen(Qt::cyan);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, "19-24", &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::cyan);
            d->m_painter.drawText(QRect(5, 5, 100, 100), 0, "19-24");
        }
    }
    d->m_painter.resetTransform();

    d->m_painter.restore();
}

void Scatter2dChart::drawBlackbodyLocus()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);
    d->m_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPen pn;
    pn.setColor(Qt::white);
    pn.setCapStyle(Qt::RoundCap);
    pn.setWidthF(1.0);

    QPen pnDash;
    pnDash.setColor(Qt::white);
    pnDash.setStyle(Qt::DashLine);
    pnDash.setCapStyle(Qt::RoundCap);
    pnDash.setWidthF(1.0);

    QPen pnOuter;
    pnOuter.setColor(Qt::black);
    pn.setCapStyle(Qt::RoundCap);
    pnOuter.setWidthF(2.0);

    d->m_painter.setFont(QFont("Courier New", 10.0));

    d->m_painter.setBrush(Qt::transparent);

    QLineF markerLong(0, -25, 0, 25);
    QLineF markerShort(0, -10, 0, 10);
    QLineF markerShorter(0, -3, 0, 3);

    const double ratio = d->m_zoomRatio * d->m_pixmapSize;
    const int labelIter = [&]() {
        if (ratio < 1.5) {
            return 25000;
        } else if (ratio < 3.0) {
            return 500;
        } else if (ratio < 5.0) {
            return 100;
        }
        return 50;
    }();

    QPainterPath daylight(mapPoint(QPointF(d->m_daylightLocus.at(0).x(), d->m_daylightLocus.at(0).y())));
    for (int i = 1; i < d->m_daylightLocus.size(); i++) {
        const QPointF dyPoint = mapPoint(QPointF(d->m_daylightLocus.at(i).x(), d->m_daylightLocus.at(i).y()));
        daylight.lineTo(dyPoint);
    }

    d->m_painter.setPen(pnOuter);
    d->m_painter.drawPath(daylight);
    d->m_painter.setPen(pnDash);
    d->m_painter.drawPath(daylight);

    QPainterPath blackbody(mapPoint(QPointF(d->m_blackbodyLocus.at(0).x(), d->m_blackbodyLocus.at(0).y())));
    for (int i = 1; i < d->m_blackbodyLocus.size(); i++) {
        const QPointF blbPoint = mapPoint(QPointF(d->m_blackbodyLocus.at(i).x(), d->m_blackbodyLocus.at(i).y()));
        blackbody.lineTo(blbPoint);
    }

    d->m_painter.setPen(pnOuter);
    d->m_painter.drawPath(blackbody);
    d->m_painter.setPen(pn);
    d->m_painter.drawPath(blackbody);

    d->m_painter.setBrush(Qt::black);

    const QVector<int> majorMarks{2000, 3000, 4000, 5000, 6500, 10000, 15000};

    for (int i = 1700; i <= 25000; i += 50) {
        const int ix = (i - 1700) / 50;
        const float theta = std::sqrt(std::fabs(2250.0 - i)) * ((i > 2250) ? -0.5 : 0.5);

        if (majorMarks.contains(i)) {
            d->m_painter.resetTransform();

            const QPointF blbPoint = mapPoint(QPointF(d->m_blackbodyLocus.at(ix).x(), d->m_blackbodyLocus.at(ix).y()));
            d->m_painter.translate(blbPoint);
            d->m_painter.rotate(theta);
            d->m_painter.setPen(pnOuter);
            d->m_painter.drawLine(markerLong);
            d->m_painter.setPen(pn);
            d->m_painter.drawLine(markerLong);

            d->m_painter.rotate(theta * -1.0);

            QRect boundRect;
            const QString locLabel = QString("%1K").arg(QString::number(i));
            d->m_painter.setPen(Qt::white);
            d->m_painter.drawText(QRect(-10, 30, 100, 100), 0, locLabel, &boundRect);
            d->m_painter.setPen(Qt::NoPen);
            d->m_painter.drawRect(boundRect);
            d->m_painter.setPen(Qt::white);
            d->m_painter.drawText(QRect(-10, 30, 100, 100), 0, locLabel);
        }

        if (i % labelIter == 0 && i < 15000 && !majorMarks.contains(i)) {
            d->m_painter.resetTransform();
            const QPointF blbPoint = mapPoint(QPointF(d->m_blackbodyLocus.at(ix).x(), d->m_blackbodyLocus.at(ix).y()));
            d->m_painter.translate(blbPoint);
            d->m_painter.rotate(theta);
            if (i % 500 == 0) {
                d->m_painter.setPen(pnOuter);
                d->m_painter.drawLine(markerShort);
                d->m_painter.setPen(pn);
                d->m_painter.drawLine(markerShort);

                d->m_painter.rotate(theta * -1.0);

                if (i < 10000 && ratio >= 5.0) {
                    QRect boundRect;
                    const QString locLabel = QString("%1K").arg(QString::number(i));
                    d->m_painter.setPen(Qt::white);
                    d->m_painter.drawText(QRect(-10, 20, 100, 100), 0, locLabel, &boundRect);
                    d->m_painter.setPen(Qt::NoPen);
                    d->m_painter.drawRect(boundRect);
                    d->m_painter.setPen(Qt::white);
                    d->m_painter.drawText(QRect(-10, 20, 100, 100), 0, locLabel);
                }
            } else {
                d->m_painter.setPen(pnOuter);
                d->m_painter.drawLine(markerShorter);
                d->m_painter.setPen(pn);
                d->m_painter.drawLine(markerShorter);
            }
        }
    }
    d->m_painter.resetTransform();

    for (int i = 4000; i <= 10000; i += 50) {
        const int ix = (i - 4000) / 50;
        const float theta = std::sqrt(std::fabs(2250.0 - i)) * ((i > 2250) ? -0.5 : 0.5);
        if (i % 1000 == 0) {
            d->m_painter.resetTransform();

            const QPointF blbPoint = mapPoint(QPointF(d->m_daylightLocus.at(ix).x(), d->m_daylightLocus.at(ix).y()));
            d->m_painter.translate(blbPoint);
            d->m_painter.rotate(theta);
            d->m_painter.setPen(pnOuter);
            d->m_painter.drawLine(markerShort);
            d->m_painter.setPen(pn);
            d->m_painter.drawLine(markerShort);

            if (i == 5000 && ratio >= 3.0) {
                d->m_painter.rotate(theta * -1.0);

                QRect boundRect;
                const QString locLabel = QString("D%1").arg(QString::number(i/100));
                d->m_painter.setPen(Qt::white);
                d->m_painter.drawText(QRect(-10, 15, 100, 100), 0, locLabel, &boundRect);
                d->m_painter.setPen(Qt::NoPen);
                d->m_painter.drawRect(boundRect);
                d->m_painter.setPen(Qt::white);
                d->m_painter.drawText(QRect(-10, 15, 100, 100), 0, locLabel);
            }
        } else if (i % 500 == 0 && ratio >= 3.0) {
            d->m_painter.resetTransform();

            const QPointF blbPoint = mapPoint(QPointF(d->m_daylightLocus.at(ix).x(), d->m_daylightLocus.at(ix).y()));
            d->m_painter.translate(blbPoint);
            d->m_painter.rotate(theta);
            d->m_painter.setPen(pnOuter);
            d->m_painter.drawLine(markerShorter);
            d->m_painter.setPen(pn);
            d->m_painter.drawLine(markerShorter);

            if (i == 6500) {
                d->m_painter.rotate(theta * -1.0);

                QRect boundRect;
                const QString locLabel = QString("D%1").arg(QString::number(i/100));
                d->m_painter.setPen(Qt::white);
                d->m_painter.drawText(QRect(-10, 10, 100, 100), 0, locLabel, &boundRect);
                d->m_painter.setPen(Qt::NoPen);
                d->m_painter.drawRect(boundRect);
                d->m_painter.setPen(Qt::white);
                d->m_painter.drawText(QRect(-10, 10, 100, 100), 0, locLabel);
            }
        }
    }
    d->m_painter.resetTransform();

    d->m_painter.restore();
}

void Scatter2dChart::drawGrids()
{
    d->m_painter.save();
    d->m_painter.setRenderHint(QPainter::Antialiasing);

    if (d->m_bgColor != Qt::black) {
        d->m_painter.setCompositionMode(QPainter::CompositionMode_Difference);
    }

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

    d->m_painter.setFont(QFont("Courier New", (d->m_zoomRatio * 12.0), QFont::Medium));
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
    if (d->m_bgColor != Qt::black) {
        d->m_painter.setCompositionMode(QPainter::CompositionMode_Difference);
    }
    d->m_painter.setPen(QPen(Qt::lightGray));
    d->m_labelFont.setPixelSize(d->m_pixmapSize * 16);
    d->m_painter.setFont(d->m_labelFont);
    const QPointF centerXY =
        mapScreenPoint({static_cast<double>(width() / 2.0 - 0.5), static_cast<double>(height() / 2 - 0.5)});

    QString fullLegends;

    if (d->renderSlices) {
        const double sliceRange = d->m_maxY - d->m_minY;
        const double sliceHalfSize = sliceRange / d->m_numberOfSlices / 2.0;
        const double currentPos =  d->m_minY + ((d->m_slicePos * 1.0 / d->m_numberOfSlices * 1.0) * sliceRange);

        const QString slicesPos =
            QString("Y: %1(±%2)\n").arg(QString::number(currentPos), QString::number(sliceHalfSize));

        fullLegends += slicesPos;
    }

    const QString legends = QString("Pixels: %4 (total)| %5 (%6)\nCenter: x:%1 | y:%2\nZoom: %3\%")
                                .arg(QString::number(centerXY.x(), 'f', 6),
                                     QString::number(centerXY.y(), 'f', 6),
                                     QString::number(d->m_zoomRatio * 100.0, 'f', 2),
                                     QString::number(d->m_cPoints->size()),
                                     QString::number(d->m_lastDrawnParticles),
                                     QString(!d->isDownscaled ? !d->finishedRender ? "rendering..." : "rendered" : "draft"));

    fullLegends += legends;

    if (d->enableStats) {
        const double mpxPerSec = d->finishedRender ? (d->m_lastDrawnParticles / 1000000.0) / (d->m_msecRenderTime / 1000.0) : d->m_mpxPerSec;
        d->m_mpxPerSec = mpxPerSec;
        const QString mpps =
            QString("\n%1 MPoints/s (%2%5) | Canvas: %3x%4")
                .arg(QString::number(mpxPerSec, 'f', 3),
                     QString(!d->isDownscaled ? d->useBucketRender ? "bucket" : "progressive" : "draft"),
                     QString::number(d->m_pixmap.width()),
                     QString::number(d->m_pixmap.height()),
                     QString(!d->isDownscaled ? d->enableAA ? ", AA" : "" : ""));

        fullLegends += mpps;
    }

    d->m_painter.drawText(d->m_pixmap.rect(), Qt::AlignBottom | Qt::AlignLeft, fullLegends);

    d->m_painter.restore();
}

void Scatter2dChart::doUpdate()
{
    d->needUpdatePixmap = false;
    d->m_pixmap = QImage(size() * devicePixelRatioF() * d->m_pixmapSize, d->m_imageFormat);
    d->m_pixmap.setDevicePixelRatio(devicePixelRatioF());
    d->m_pixmap.setColorSpace(d->m_imageSpace);
    d->m_pixmap.fill(d->m_bgColor);

    if (d->keepCentered) {
        d->keepCentered = false;
        const QPointF centerDelta =
            mapScreenPoint({static_cast<double>(width() / 2.0 - 0.5), static_cast<double>(height() / 2.0 - 0.5)})
            - d->m_lastCenter;
        const RenderBounds rb = getRenderBounds();
        const QPointF originMinusDelta = QPointF(rb.originX, rb.originY) - centerDelta;
        d->m_offsetX = originMinusDelta.x() * -1.0 * oneUnitInPx();
        d->m_offsetY = originMinusDelta.y() * -1.0 * oneUnitInPx();
    } else {
        d->m_lastCenter =
            mapScreenPoint({static_cast<double>(width() / 2.0 - 0.5), static_cast<double>(height() / 2.0 - 0.5)});
    }

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

    if (d->enableMacAdamEllipses) {
        drawMacAdamEllipses();
    }

    if (d->enableColorCheckerPoints76) {
        drawColorCheckerPoints76();
    }

    if (d->enableColorCheckerPointsOld) {
        drawColorCheckerPoints();
    }

    if (d->enableColorCheckerPoints) {
        drawColorCheckerPointsNew();
    }

    if (d->enableBlackbodyLocus) {
        drawBlackbodyLocus();
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
    p.drawImage(0, 0, d->m_pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (!d->isDownscaled) {
        setCursor(Qt::ArrowCursor);
    }
}

void Scatter2dChart::drawFutureAt(int ft)
{
    if (d->renderSlices || d->m_future.isCanceled()) return;

    const auto resu = d->m_future.resultAt(ft);

    QPainter tempPainter;

    tempPainter.begin(&d->m_ScatterPixmap);
    tempPainter.setCompositionMode(QPainter::CompositionMode_Lighten);

    tempPainter.drawImage(resu.second, resu.first);

    tempPainter.end();

    d->needUpdatePixmap = true;
    update();
}

void Scatter2dChart::cancelRender()
{
    if(d->m_future.isRunning()) {
        d->m_locker.lock();
        d->isCancelFired = true;
        d->m_locker.unlock();
        d->m_future.cancel();
        d->m_future.waitForFinished();
    }
}

void Scatter2dChart::onFinishedDrawing()
{
    if (!d->m_future.isCanceled()) {
        d->finishedRender = true;
        d->needUpdatePixmap = true;
        if (d->m_renderTimer.isValid()) {
            d->m_msecRenderTime = d->m_renderTimer.elapsed();
        }
        update();
    }
}

void Scatter2dChart::whenScrollTimerEnds()
{
    // render at full again
    if (d->isMouseHold)
        return;
    d->inputScatterData = true;
    d->m_ScatterPixmap = QImage(d->m_pixmap.size(), d->m_imageFormat);
    d->m_ScatterPixmap.setColorSpace(d->m_imageSpace);
    d->m_ScatterPixmap.fill(Qt::transparent);

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
    cancelRender();
    d->finishedRender = false;
    d->isCancelFired = false;
}

void Scatter2dChart::saveSlicesAsImage()
{
    const QString tmpFileName = QFileDialog::getSaveFileName(this,
                                                             tr("Save plot as image"),
                                                             "",
                                                             tr("Portable Network Graphics (*.png)"));
    if (tmpFileName.isEmpty()) {
        return;
    }
    QString fileNameTrimmed = tmpFileName.chopped(4);

    bool isNumSlicesOkay;
    const int numSlices = QInputDialog::getInt(this,
                                               "Set number of slices",
                                               "Total slices",
                                               10,
                                               5,
                                               500,
                                               1,
                                               &isNumSlicesOkay);
    if (!isNumSlicesOkay) {
        return;
    }

    d->renderSlices = true;
    d->m_numberOfSlices = numSlices;

    QProgressDialog pDial;
    pDial.setMinimum(0);
    pDial.setMaximum(numSlices);
    pDial.setLabelText("Rendering slices...");
    pDial.setCancelButtonText("Stop");

    bool isCancelled = false;
    connect(&pDial, &QProgressDialog::canceled, [&isCancelled] {
        isCancelled = true;
    });

    pDial.show();

    for (int i = 0; i <= numSlices; i++) {
        QCoreApplication::processEvents();
        if (isCancelled) {
            d->renderSlices = false;
            return;
        }

        d->m_slicePos = i;
        doUpdate();

        const QString outputFile = fileNameTrimmed + tr("_") + QString::number(i).rightJustified(4, '0') + tr(".png");
        QImage out(d->m_pixmap);
        out.save(outputFile);
        pDial.setValue(i);
    }

    d->renderSlices = false;

    drawDownscaled(20);
    d->needUpdatePixmap = true;
    update();
}

void Scatter2dChart::resizeEvent(QResizeEvent *event)
{
    if (d->renderSlices) {
        event->ignore();
        return;
    }
    QWidget::resizeEvent(event);
    // downscale
    if (!d->m_pixmap.isNull()) {
        d->keepCentered = true;
    }

    drawDownscaled(100);
    d->needUpdatePixmap = true;
}

void Scatter2dChart::wheelEvent(QWheelEvent *event)
{
    if (d->renderSlices) {
        event->ignore();
        return;
    }

    const QPoint numDegrees = event->angleDelta();

    // exponential zooming, so that it will feel linear especially
    // on high zooom rate
    const double zoomIncrement = (numDegrees.y() / 1200.0) * d->m_zoomRatio;

    event->accept();

    // Zoom cap (around 25% - 20000%)
    if (d->m_zoomRatio + zoomIncrement > 0.25 && d->m_zoomRatio + zoomIncrement < 200.0) {
        drawDownscaled(200);

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

        d->m_offsetX -= zoomIncrement * (windowSceneCurAbsPos.x() * windowUnit);
        d->m_offsetY -= zoomIncrement * (windowSceneCurAbsPos.y() * windowUnit);

        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::mousePressEvent(QMouseEvent *event)
{
    if (d->renderSlices) {
        event->ignore();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::OpenHandCursor);
        d->m_lastPos = event->pos();
    }
}

void Scatter2dChart::mouseMoveEvent(QMouseEvent *event)
{
    if (d->renderSlices) {
        event->ignore();
        return;
    }
    if (event->buttons() & Qt::LeftButton) {
        setCursor(Qt::ClosedHandCursor);
        d->isMouseHold = true;
        drawDownscaled(20);

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
    if (d->renderSlices) {
        event->ignore();
        return;
    }
    setCursor(Qt::ArrowCursor);
    if ((event->button() == Qt::LeftButton) && d->isMouseHold) {
        d->isMouseHold = false;
        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::contextMenuEvent(QContextMenuEvent *event)
{
    if (d->renderSlices) {
        event->ignore();
        return;
    }
    const QPointF relMousePos =
        mapScreenPoint({static_cast<double>(event->pos().x()), static_cast<double>(event->pos().y())});
    const QString sizePos = QString("Cursor: x: %1 | y: %2 | %3\%")
                                .arg(QString::number(relMousePos.x(), 'f', 6),
                                     QString::number(relMousePos.y(), 'f', 6),
                                     QString::number(d->m_zoomRatio * 100.0, 'f', 2));
    QMenu menu(this);
    QMenu extra(this);
    QMenu showOverlays(this);

    menu.addAction(sizePos);

    menu.addSeparator();
    menu.addAction(d->setZoom);
    menu.addAction(d->setOrigin);

    menu.addSeparator();
    menu.addAction(d->copyOrigAndZoom);
    menu.addAction(d->pasteOrigAndZoom);

    menu.addSeparator();
    showOverlays.setTitle("Overlays");
    menu.addMenu(&showOverlays);
    showOverlays.addAction(d->drawLabels);
    showOverlays.addAction(d->drawGrids);
    showOverlays.addAction(d->drawSrgbGamut);
    showOverlays.addAction(d->drawImgGamut);
    showOverlays.addAction(d->drawMacAdamEllipses);
    showOverlays.addAction(d->drawBlackbodyLocus);
    showOverlays.addSeparator();
    showOverlays.addAction(d->drawColorCheckerPoints);
    showOverlays.addAction(d->drawColorCheckerPointsOld);
    showOverlays.addAction(d->drawColorCheckerPoints76);

    menu.addSeparator();
    menu.addAction(d->setAntiAliasing);
    menu.addAction(d->setAlpha);
    menu.addAction(d->setParticleSize);
    menu.addAction(d->setBgColor);

    extra.setTitle("Extra options");
    menu.addMenu(&extra);
    extra.addAction(d->drawStats);
    extra.addAction(d->setStaticDownscale);
    extra.addSeparator();
    extra.addAction(d->setPixmapSize);
    extra.addAction(d->saveSlicesAsImage);
    extra.addSeparator();
    extra.addAction(d->use16Bit);

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
        QInputDialog::getDouble(this, "Set zoom", "Zoom", currentZoom, 25.0, 20000.0, 1, &isZoomOkay);
    if (isZoomOkay) {
        d->keepCentered = true;
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
    const QPointF currentMid = mapScreenPoint({width() / 2.0 - 0.5, height() / 2.0 - 0.5});
    bool isXOkay(false);
    bool isYOkay(false);
    const double setX = QInputDialog::getDouble(this, "Set origin", "Origin X", currentMid.x(), -0.1, 1.0, 5, &isXOkay);
    const double setY = QInputDialog::getDouble(this, "Set origin", "Origin Y", currentMid.y(), -0.1, 1.0, 5, &isYOkay);
    if (isXOkay && isYOkay) {
        d->m_lastCenter = QPointF(setX, setY);
        d->keepCentered = true;
        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::copyOrigAndZoom()
{
    const double currentZoom = d->m_zoomRatio * 100.0;
    const QPointF currentMid = mapScreenPoint({width() / 2.0 - 0.5, height() / 2.0 - 0.5});

    const QString clipText = QString("QtGamutPlotter:%1;%2;%3;%4;%5")
                                 .arg(QString::number(currentZoom, 'f', 3),
                                      QString::number(currentMid.x(), 'f', 8),
                                      QString::number(currentMid.y(), 'f', 8),
                                      QString::number(d->m_pointOpacity),
                                      QString::number(d->m_particleSizeStored));

    d->m_clipb->setText(clipText);
}

void Scatter2dChart::pasteOrigAndZoom()
{
    QString fromClip = d->m_clipb->text();
    if (fromClip.contains("QtGamutPlotter:")) {
        const QString cleanClip = fromClip.replace("QtGamutPlotter:", "");
        const QStringList parsed = cleanClip.split(";");

        if (parsed.size() == 5) {
            const double setZoom = parsed.at(0).toDouble() / 100.0;
            if (setZoom > 0.25 && setZoom < 200.0) {
                d->m_zoomRatio = setZoom;
            }

            const double setX = parsed.at(1).toDouble();
            const double setY = parsed.at(2).toDouble();

            const int setAlpha = parsed.at(3).toInt();
            const int setSize = parsed.at(4).toInt();

            if (setAlpha >= 0.001 && setAlpha <= 1.0) {
                d->m_pointOpacity = setAlpha;
            }

            if (setSize >= 1 && setSize < 11) {
                d->m_particleSizeStored = setSize;
            }

            if ((setX > -1.0 && setX < 1.0) && (setY > -1.0 && setY < 1.0)) {
                d->m_lastCenter = QPointF(setX, setY);
                d->keepCentered = true;

                drawDownscaled(20);
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
    d->enableMacAdamEllipses = d->drawMacAdamEllipses->isChecked();
    d->enableColorCheckerPoints = d->drawColorCheckerPoints->isChecked();
    d->enableColorCheckerPointsOld = d->drawColorCheckerPointsOld->isChecked();
    d->enableColorCheckerPoints76 = d->drawColorCheckerPoints76->isChecked();
    d->enableBlackbodyLocus = d->drawBlackbodyLocus->isChecked();
    d->enableStaticDownscale = d->setStaticDownscale->isChecked();
    d->enableAA = d->setAntiAliasing->isChecked();
    d->enableStats = d->drawStats->isChecked();

    if (d->use16Bit->isChecked()) {
        d->enable16Bit = true;
        d->m_imageFormat = QImage::Format_RGBA64;
    } else if (!d->use16Bit->isChecked()) {
        d->enable16Bit = false;
        d->m_imageFormat = QImage::Format_ARGB32;
    }

    drawDownscaled(20);
    d->needUpdatePixmap = true;
    update();
}

void Scatter2dChart::changeAlpha()
{
//    const double currentAlpha = d->m_cPoints.at(0).second.alphaF();
    const double currentAlpha = d->m_pointOpacity;
    bool isAlphaOkay(false);
    const double setAlpha = QInputDialog::getDouble(this,
                                                    "Set alpha",
                                                    "Per-particle alpha",
                                                    currentAlpha,
                                                    0.001,
                                                    1.0,
                                                    3,
                                                    &isAlphaOkay,
                                                    Qt::WindowFlags(),
                                                    0.1);
    if (isAlphaOkay) {
        d->m_pointOpacity = setAlpha;

        drawDownscaled(20);
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
        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::changePixmapSize()
{
    const double currentPixmapSize = d->m_pixmapSize;
    bool isPixSizeOkay(false);
    const double setPixmapSize = QInputDialog::getDouble(this,
                                                         "Set render scaling",
                                                         "Scale",
                                                         currentPixmapSize,
                                                         1.0,
                                                         8.0,
                                                         1,
                                                         &isPixSizeOkay,
                                                         Qt::WindowFlags(),
                                                         0.1);
    if (isPixSizeOkay) {
        d->m_pixmapSize = setPixmapSize;
        d->m_offsetX = d->m_offsetX * (d->m_pixmapSize / currentPixmapSize);
        d->m_offsetY = d->m_offsetY * (d->m_pixmapSize / currentPixmapSize);

        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::changeBgColor(){
    const QColor currentBg = d->m_bgColor;
    const QColor setBgColor = QColorDialog::getColor(currentBg, this, "Set background color", QColorDialog::ShowAlphaChannel);
    if (setBgColor.isValid()) {
        d->m_bgColor = setBgColor;

        drawDownscaled(20);
        d->needUpdatePixmap = true;
        update();
    }
}

void Scatter2dChart::resetCamera()
{
    drawDownscaled(20);

    d->m_zoomRatio = 1.1;
    d->m_offsetX = 100;
    d->m_offsetY = 50;

    d->needUpdatePixmap = true;
    update();
}

QImage *Scatter2dChart::getFullPixmap()
{
    if (d->m_future.isRunning()) {
        d->m_future.waitForFinished();
    }
    return &d->m_pixmap;
}
