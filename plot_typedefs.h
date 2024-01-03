#ifndef PLOT_TYPEDEFS_H
#define PLOT_TYPEDEFS_H

#include <QVector3D>
#include <QColor>

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

typedef struct {
    bool enableAA;
    bool forceBucket;
    bool use16Bit;
    bool showStatistics;
    bool showGridsAndSpectrum;
    bool showsRGBGamut;
    bool showImageGamut;
    bool showMacAdamEllipses;
    bool showColorCheckerPoints;
    bool showBlBodyLocus;
    double particleOpacity;
    int particleSize;
    double renderScale;
} PlotSetting2D;

#endif // PLOT_TYPEDEFS_H
