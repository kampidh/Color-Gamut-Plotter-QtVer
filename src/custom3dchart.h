#ifndef CUSTOM3DCHART_H
#define CUSTOM3DCHART_H

#include <QOpenGLWidget>
#include <QKeyEvent>
#include <QScopedPointer>

#include "plot_typedefs.h"

class Custom3dChart : public QOpenGLWidget
{
    Q_OBJECT
public:
    Custom3dChart(PlotSetting2D &plotSetting, QWidget *parent = nullptr);
    ~Custom3dChart();

    void addDataPoints(QVector<ColorPoint> &dArray, QVector3D &dWhitePoint, QVector<ImageXYZDouble> &dOutGamut);
    void passKeypres(QKeyEvent *e);

    void resetCamera();
    bool checkValidity();
    QImage takeTheShot();

private slots:
    void changeBgColor();
    void changeMonoColor();
    void copyState();
    void pasteState();
    void changeUpscaler();

private:
    void initializeGL() override;
    void paintGL() override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    void doUpdate();
    void doNavigation();
    void reloadShaders();
    void cycleModes(const bool &changeTarget = true);

    class Private;
    QScopedPointer<Private> d;
};

#endif // CUSTOM3DCHART_H
