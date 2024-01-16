#ifndef CUSTOM3DCHART_H
#define CUSTOM3DCHART_H

#include <QOpenGLWidget>
#include <QKeyEvent>

#include "plot_typedefs.h"

class Custom3dChart : public QOpenGLWidget
{
    Q_OBJECT
public:
    Custom3dChart(QWidget *parent = nullptr);
    ~Custom3dChart();

    void addDataPoints(QVector<ColorPoint> &dArray, QVector3D &dWhitePoint);
    void passKeypres(QKeyEvent *e);

    void resetCamera();
    bool checkValidity();

private slots:
    void changeBgColor();

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

    class Private;
    Private *const d{nullptr};
};

#endif // CUSTOM3DCHART_H
