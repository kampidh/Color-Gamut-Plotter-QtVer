#ifndef PLOT_TYPEDEFS_H
#define PLOT_TYPEDEFS_H

#include <QVector3D>
#include <QColor>

typedef struct IXYZDouble {
    double X;
    double Y;
    double Z;

    friend bool operator<(const IXYZDouble &lhs, const IXYZDouble &rhs) {
        const double in =
            // std::pow(lhs.X, 2) + std::pow(lhs.Y, 3) * std::pow(2, lhs.X) + std::pow(lhs.Z, 4) * std::pow(3, lhs.Y);
            lhs.X + (2 * lhs.Y) + (3 * lhs.Z);
        const double out =
            // std::pow(rhs.X, 2) + std::pow(rhs.Y, 3) * std::pow(2, rhs.X) + std::pow(rhs.Z, 4) * std::pow(3, rhs.Y);
            rhs.X + (2 * rhs.Y) + (3 * rhs.Z);
        return in < out;
    }
    inline bool operator>(IXYZDouble &rhs) { return rhs < *this; }
    inline bool operator<=(IXYZDouble &rhs) { return !(*this > rhs); }
    inline bool operator>=(IXYZDouble &rhs) { return !(*this < rhs); }
} ImageXYZDouble;

typedef struct IRGBFloat {
    float R;
    float G;
    float B;

    friend bool operator<(const IRGBFloat &lhs, const IRGBFloat &rhs) {
        const double in =
            // std::pow(lhs.R, 2) + std::pow(lhs.G, 3) * std::pow(2, lhs.R) + std::pow(lhs.B, 4) * std::pow(3, lhs.G);
            lhs.R + (2 * lhs.G) + (3 * lhs.B);
        const double out =
            // std::pow(rhs.R, 2) + std::pow(rhs.G, 3) * std::pow(2, rhs.R) + std::pow(rhs.B, 4) * std::pow(3, rhs.G);
            lhs.R + (2 * lhs.G) + (3 * lhs.B);
        return in < out;
    }
    inline bool operator>(IRGBFloat &rhs) { return rhs < *this; }
    inline bool operator<=(IRGBFloat &rhs) { return !(*this > rhs); }
    inline bool operator>=(IRGBFloat &rhs) { return !(*this < rhs); }
} ImageRGBFloat;

typedef QPair<ImageXYZDouble, ImageRGBFloat> ColorPoint;
typedef QMap<ImageXYZDouble, ImageRGBFloat> ColorPointMap;
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
