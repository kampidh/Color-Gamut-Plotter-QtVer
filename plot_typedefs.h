#ifndef PLOT_TYPEDEFS_H
#define PLOT_TYPEDEFS_H

#include <QVector3D>

typedef struct {
    double X;
    double Y;
    double Z;
} ImageXYZDouble;

typedef QPair<ImageXYZDouble, QColor> ColorPoint;
typedef QPair<QPointF, QColor> ColorPointMapped;

#endif // PLOT_TYPEDEFS_H
