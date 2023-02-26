/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "scatter3dchart.h"

#include <QtDataVisualization/q3dcamera.h>
#include <QtDataVisualization/q3dscene.h>
#include <QtDataVisualization/q3dtheme.h>
#include <QtDataVisualization/qscatter3dseries.h>
#include <QtDataVisualization/qscatterdataitem.h>
#include <QtDataVisualization/qscatterdataproxy.h>
#include <QtDataVisualization/qvalue3daxis.h>

#include <QApplication>
#include <QColorSpace>
#include <QImage>
#include <QScreen>
#include <QProgressDialog>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

#include <algorithm>
#include <cmath>

#include <lcms2.h>

using namespace QtDataVisualization;

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

class Q_DECL_HIDDEN Scatter3dChart::Private
{
public:
    // unused, reserved
};

Scatter3dChart::Scatter3dChart(const QSurfaceFormat *format, QWindow *parent)
    : Q3DScatter{format, parent}
    , d(new Private)
{

//    d->m_graph = m_graph;
    activeTheme()->setType(Q3DTheme::ThemeEbony);
    QFont font = activeTheme()->font();
    font.setPointSize(12.0f);
    activeTheme()->setFont(font);
    setShadowQuality(QAbstract3DGraph::ShadowQualityNone);
    scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetFront);
    activeTheme()->setBackgroundEnabled(false);

    axisX()->setTitle("x");
    axisY()->setTitle("Y");
    axisZ()->setTitle("y");

    axisX()->setRange(-0.1, 0.9);
    axisZ()->setRange(-0.1, 0.9);

    activeTheme()->setLightStrength(0.0);
    activeTheme()->setAmbientLightStrength(1.0);
    activeTheme()->setGridLineColor(QColor(20, 20, 20));

    scene()->activeCamera()->setZoomLevel(120.0);
}

Scatter3dChart::~Scatter3dChart()
{
    delete d;
}

void Scatter3dChart::addDataPoints(QVector<QVector3D> &dArray,
                                   QVector<QColor> &dColor,
                                   QVector<QVector3D> &dGamut,
                                   bool isSrgb,
                                   int type)
{
    cmsSetAdaptationState(0.0);

    const cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();
    const cmsHPROFILE hsXYZ = cmsCreateXYZProfile();

    const cmsHTRANSFORM xform =
        cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);

    QVector<QVector3D> gamutsRGB;

    for (int i = 0; i < 256; i++) {
        const QVector<QColor> gmt = {QColor(i, 255 - i, 0), QColor(0, i, 255 - i), QColor(255 - i, 0, i)};
        for (auto &clr : gmt) {
            cmsCIEXYZ bufXYZ;
            cmsCIExyY bufXYY;
            const double pix[3] = {clr.redF(), clr.greenF(), clr.blueF()};
            cmsDoTransform(xform, &pix, &bufXYZ, 1);
            cmsXYZ2xyY(&bufXYY, &bufXYZ);
            gamutsRGB.append(QVector3D(bufXYY.x, bufXYY.y, bufXYY.Y));
        }
    }

    QProgressDialog *pDial = new QProgressDialog();
    pDial->setMinimum(0);
    pDial->setMaximum(dArray.size());
    pDial->setLabelText("Plotting...");
    pDial->setCancelButtonText("Stop");

    pDial->show();

         // Input main data
    {
        // No need to call data proxy I guess..
        // QScatterDataProxy *proxy = new QScatterDataProxy;
        QScatter3DSeries *series = new QScatter3DSeries();
        series->setItemLabelFormat(QStringLiteral("@xTitle: @xLabel @zTitle: @zLabel @yTitle: @yLabel"));
        series->setMesh(QAbstract3DSeries::MeshPoint);
        series->setMeshSmooth(false);
        series->setItemSize(0.05);
        series->setBaseColor(Qt::yellow);
        uint32_t progress = 0;
        uint32_t progDivider = dArray.size() / 10;
        for (uint32_t i = 0; i < dArray.size(); i++) {
            if (type > 0) {
                inputRGBDataVec(dArray[i], dColor[i], 0.05, false);
            } else {
                inputMonoDataVec(dArray[i], series, false);
            }
            progress++;
            if (progress % progDivider == 0) {
                pDial->setValue(progress);
                if (pDial->wasCanceled()) {
                    break;
                }
                QApplication::processEvents();
            }
        }
        if (type > 0) {
            delete series;
        } else {
            addSeries(series);
        }
        pDial->setValue(progress);
    }

         // Draw sRGB gamut triangle
    if (!isSrgb) {
        QScatter3DSeries *series = new QScatter3DSeries();
        series->setItemLabelFormat(QStringLiteral("@xTitle: @xLabel @zTitle: @zLabel @yTitle: @yLabel"));
        series->setMesh(QAbstract3DSeries::MeshPoint);
        series->setMeshSmooth(false);
        series->setItemSize(0.01);
        series->setBaseColor(QColor(96, 96, 96));
        for (uint32_t i = 0; i < gamutsRGB.size(); i++) {
            inputMonoDataVec(gamutsRGB[i], series, true);
        }
        addSeries(series);
    }

         // Draw current profile gamut triangle
    {
        QScatter3DSeries *series = new QScatter3DSeries();
        series->setItemLabelFormat(QStringLiteral("@xTitle: @xLabel @zTitle: @zLabel @yTitle: @yLabel"));
        series->setMesh(QAbstract3DSeries::MeshPoint);
        series->setMeshSmooth(false);
        series->setItemSize(0.02);
        series->setBaseColor(Qt::darkRed);
        for (uint32_t i = 0; i < dGamut.size(); i++) {
            inputMonoDataVec(dGamut[i], series, true);
        }
        addSeries(series);
    }

         // Draw tongue gamut
    {
        QScatter3DSeries *series = new QScatter3DSeries();
        series->setItemLabelFormat(QStringLiteral("@xTitle: @xLabel @zTitle: @zLabel @yTitle: @yLabel"));
        series->setMesh(QAbstract3DSeries::MeshPoint);
        series->setMeshSmooth(false);
        series->setItemSize(0.01);
        series->setBaseColor(QColor(64, 64, 64));
        const int interpNum = 20;
        for (int i = 0; i < 80; i++) {
            // Interpolate the points
            for (int j = 0; j < interpNum; j++) {
                QScatterDataItem item;
                const double posX = (spectral_chromaticity[i][0] * (1.0 - (j / static_cast<float>(interpNum))))
                    + (spectral_chromaticity[i + 1][0] * (j / static_cast<float>(interpNum)));
                const double posY = (spectral_chromaticity[i][1] * (1.0 - (j / static_cast<float>(interpNum))))
                    + (spectral_chromaticity[i + 1][1] * (j / static_cast<float>(interpNum)));
                item.setPosition(QVector3D(posX, 0.0, posY));
                series->dataProxy()->addItem(item);
            }
        }
        // Interpolate line of purples
        const int intPurpNum = interpNum * 10;
        for (int i = 0; i < intPurpNum; i++) {
            QScatterDataItem item;
            const double posX = (spectral_chromaticity[0][0] * (1.0 - (i / static_cast<float>(intPurpNum))))
                + (spectral_chromaticity[80][0] * (i / static_cast<float>(intPurpNum)));
            const double posY = (spectral_chromaticity[0][1] * (1.0 - (i / static_cast<float>(intPurpNum))))
                + (spectral_chromaticity[80][1] * (i / static_cast<float>(intPurpNum)));
            item.setPosition(QVector3D(posX, 0.0, posY));
            series->dataProxy()->addItem(item);
        }
        addSeries(series);
    }

    cmsDeleteTransform(xform);
}

void Scatter3dChart::inputRGBDataVec(QVector3D &xyy, QColor col, float size, bool flatten)
{
    QScatter3DSeries *series = new QScatter3DSeries();
    series->setItemLabelFormat(QStringLiteral("@xTitle: @xLabel @zTitle: @zLabel @yTitle: @yLabel"));
    series->setMesh(QAbstract3DSeries::MeshPoint);
    series->setMeshSmooth(false);
    series->setItemSize(size);

    QScatterDataItem item;

         // Don't plot if NaN exists
    if (xyy.x() != xyy.x() || xyy.y() != xyy.y() || xyy.z() != xyy.z()) {
        delete series;
        return;
    }

    series->setBaseColor(col);

    if (!flatten) {
        item.setPosition(QVector3D(xyy.x(), xyy.z(), xyy.y()));
    } else {
        item.setPosition(QVector3D(xyy.x(), 0.0, xyy.y()));
    }

    series->dataProxy()->addItem(item);

    addSeries(series);
}

void Scatter3dChart::inputMonoDataVec(QVector3D &xyy, QScatter3DSeries *series, bool flatten)
{
    QScatterDataItem item;

         // Don't plot if NaN exists
    if (xyy.x() != xyy.x() || xyy.y() != xyy.y() || xyy.z() != xyy.z()) {
        return;
    }

    if (!flatten) {
        item.setPosition(QVector3D(xyy.x(), xyy.z(), xyy.y()));
    } else {
        item.setPosition(QVector3D(xyy.x(), 0.0, xyy.y()));
    }

    series->dataProxy()->addItem(item);
}

void Scatter3dChart::setOrthogonal(bool set)
{
    setOrthoProjection(set);
    if (set) {
        scene()->activeCamera()->setZoomLevel(100.0);
    } else {
        scene()->activeCamera()->setZoomLevel(120.0);
    }
}

void Scatter3dChart::changePresetCamera()
{
    scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetDirectlyAbove);
    if (isOrthoProjection()) {
        scene()->activeCamera()->setZoomLevel(100.0);
    } else {
        scene()->activeCamera()->setZoomLevel(120.0);
    }
}
