#ifndef PLOT_TYPEDEFS_H
#define PLOT_TYPEDEFS_H

#include <QVector3D>

typedef struct {
    double X;
    double Y;
    double Z;
} ImageXYZDouble;

typedef struct {
    float R;
    float G;
    float B;
} ImageRGBFloat;

typedef QPair<ImageXYZDouble, ImageRGBFloat> ColorPoint;
typedef QPair<QPointF, QColor> ColorPointMapped; // this one is unused

#endif // PLOT_TYPEDEFS_H
