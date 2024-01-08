/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#ifndef SCATTER3DCHART_H
#define SCATTER3DCHART_H

#include <QVector3D>
#include <QWidget>
#include <QtDataVisualization/q3dscatter.h>

#include <lcms2.h>

#include "plot_typedefs.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
using namespace QtDataVisualization;
#endif

class Scatter3dChart : public Q3DScatter
{
    Q_OBJECT
public:
    explicit Scatter3dChart(const QSurfaceFormat *format = nullptr, QWindow *parent = nullptr);
    ~Scatter3dChart();

    void addDataPoints(QVector<ColorPoint> &dArray, QVector<ImageXYZDouble> &dGamut, bool isSrgb, int type);

    void changePresetCamera();

public slots:
    void setOrthogonal(bool set);

private:
    void inputRGBDataVec(ImageXYZDouble &xyy, ImageRGBFloat col, float size, bool flatten);
    void inputMonoDataVec(ImageXYZDouble &xyy, QScatter3DSeries *series, bool flatten);

    class Private;
    Private *const d{nullptr};
};

#endif // SCATTER3DCHART_H
