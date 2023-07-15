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

#include "plot_typedefs.h"

class Scatter2dChart : public QWidget
{
    Q_OBJECT
public:
    explicit Scatter2dChart(QWidget *parent = nullptr);
    ~Scatter2dChart();

    void overrideSettings(PlotSetting2D &plot);
    void addDataPoints(QVector<ColorPoint> &dArray, int size = 100);
    void addGamutOutline(QVector<ImageXYZDouble> &dOutGamut, QVector3D &dWhitePoint);
    void resetCamera();
    QPixmap *getFullPixmap();

    void cancelRender();

    typedef struct {
        double originX;
        double originY;
        double maxX;
        double maxY;
    } RenderBounds;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *) override;

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void changeZoom();
    void changeOrigin();
    void copyOrigAndZoom();
    void pasteOrigAndZoom();
    void changeProperties();
    void changeAlpha();
    void changeParticleSize();
    void changePixmapSize();
    void changeBgColor();
    void saveSlicesAsImage();
    void drawFutureAt(int ft);
    void onFinishedDrawing();

private:
    void drawDataPoints();
    void drawSpectralLine();
    void drawSrgbTriangle();
    void drawGamutTriangleWP();
    void drawMacAdamEllipses();
    void drawColorCheckerPoints();
    void drawGrids();
    void drawLabels();
    void doUpdate();
    void whenScrollTimerEnds();
    void drawDownscaled(int delayms);

    QPointF mapPoint(QPointF xy) const;
    QPointF mapScreenPoint(QPointF xy) const;
    double oneUnitInPx() const;
    RenderBounds getRenderBounds() const;

    class Private;
    Private *const d{nullptr};
};

#endif // SCATTER2DCHART_H
