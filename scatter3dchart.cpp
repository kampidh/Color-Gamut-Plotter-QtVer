/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "scatter3dchart.h"
#include "constant_dataset.h"

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
#include <QLabel>
#include <QProgressDialog>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include <lcms2.h>

using namespace QtDataVisualization;

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

void Scatter3dChart::addDataPoints(QVector<ColorPoint> &dArray, QVector<ImageXYZDouble> &dGamut, bool isSrgb, int type)
{
    cmsSetAdaptationState(0.0);

    const cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();
    const cmsHPROFILE hsXYZ = cmsCreateXYZProfile();

    const cmsHTRANSFORM xform =
        cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);

    QVector<ImageXYZDouble> gamutsRGB;

    for (int i = 0; i < 256; i++) {
        const QVector<QColor> gmt = {QColor(i, 255 - i, 0), QColor(0, i, 255 - i), QColor(255 - i, 0, i)};
        for (auto &clr : gmt) {
            cmsCIEXYZ bufXYZ;
            cmsCIExyY bufXYY;
            const double pix[3] = {clr.redF(), clr.greenF(), clr.blueF()};
            cmsDoTransform(xform, &pix, &bufXYZ, 1);
            cmsXYZ2xyY(&bufXYY, &bufXYZ);
            gamutsRGB.append({bufXYY.x, bufXYY.y, bufXYY.Y});
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
                inputRGBDataVec(dArray[i].first, dArray[i].second, 0.05, false);
            } else {
                inputMonoDataVec(dArray[i].first, series, false);
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

void Scatter3dChart::inputRGBDataVec(ImageXYZDouble &xyy, ImageRGBFloat col, float size, bool flatten)
{
    QScatter3DSeries *series = new QScatter3DSeries();
    series->setItemLabelFormat(QStringLiteral("@xTitle: @xLabel @zTitle: @zLabel @yTitle: @yLabel"));
    series->setMesh(QAbstract3DSeries::MeshPoint);
    series->setMeshSmooth(false);
    series->setItemSize(size);

    QScatterDataItem item;

    // Don't plot if NaN exists
    if (xyy.X != xyy.X || xyy.Y != xyy.Y || xyy.Z != xyy.Z) {
        delete series;
        return;
    }

    QColor cols;
    cols.setRgbF(col.R, col.G, col.B);

    series->setBaseColor(cols);

    if (!flatten) {
        item.setPosition(QVector3D(xyy.X, xyy.Z, xyy.Y));
    } else {
        item.setPosition(QVector3D(xyy.X, 0.0, xyy.Z));
    }

    series->dataProxy()->addItem(item);

    addSeries(series);
}

void Scatter3dChart::inputMonoDataVec(ImageXYZDouble &xyy, QScatter3DSeries *series, bool flatten)
{
    QScatterDataItem item;

    // Don't plot if NaN exists
    if (xyy.X != xyy.X || xyy.Y != xyy.Y || xyy.Z != xyy.Z) {
        return;
    }

    if (!flatten) {
        item.setPosition(QVector3D(xyy.X, xyy.Z, xyy.Y));
    } else {
        item.setPosition(QVector3D(xyy.X, 0.0, xyy.Y));
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
