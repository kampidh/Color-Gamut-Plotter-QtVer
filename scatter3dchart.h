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

using namespace QtDataVisualization;

class Scatter3dChart : public Q3DScatter
{
    Q_OBJECT
public:
    explicit Scatter3dChart(const QSurfaceFormat *format = nullptr, QWindow *parent = nullptr);
    ~Scatter3dChart();

    void addDataPoints(QVector<QVector3D> &dArray,
                       QVector<QColor> &dColor,
                       QVector<QVector3D> &dGamut,
                       bool isSrgb,
                       int type);

    void changePresetCamera();

public slots:
    void setOrthogonal(bool set);

private:
    void inputRGBDataVec(QVector3D &xyy, QColor col, float size, bool flatten);
    void inputMonoDataVec(QVector3D &xyy, QScatter3DSeries *series, bool flatten);

    class Private;
    Private *const d{nullptr};
};

#endif // SCATTER3DCHART_H
