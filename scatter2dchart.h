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

#ifndef SCATTER2DCHART_H
#define SCATTER2DCHART_H

#include <QVector3D>
#include <QWidget>

class Scatter2dChart : public QWidget
{
    Q_OBJECT
public:
    explicit Scatter2dChart(QWidget *parent = nullptr);
    ~Scatter2dChart();

    void addDataPoints(QVector<QVector3D> &dArray, QVector<QColor> &dColor, int size = 100);
    void addGamutOutline(QVector<QVector3D> &dOutGamut, QVector2D &dWhitePoint);
    void resetCamera();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *) override;

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void drawDataPoints();
    void drawSpectralLine();
    void drawSrgbTriangle();
    void drawGamutTriangleWP();
    void drawGrids();
    void drawLabels();
    void doUpdate();
    void whenScrollTimerEnds();
    void drawDownscaled(int delayms);
    QPoint mapPoint(QPointF xy);

    class Private;
    Private *const d{nullptr};
};

#endif // SCATTER2DCHART_H
