#ifndef PLOT_TYPEDEFS_H
#define PLOT_TYPEDEFS_H

#include <QVector3D>
#include <cmath>

typedef struct IXYZDouble {
    double X;
    double Y;
    double Z;

    friend bool operator<(const IXYZDouble &lhs, const IXYZDouble &rhs) {
        const double in = lhs.X + (2 * lhs.Y) + (3 * lhs.Z);
        const double out = rhs.X + (2 * rhs.Y) + (3 * rhs.Z);
        if (std::isnan(in) || std::isnan(out)) return false;
        return in < out;
    }
    friend bool operator==(const IXYZDouble &lhs, const IXYZDouble &rhs) {
        const double in = lhs.X + (2 * lhs.Y) + (3 * lhs.Z);
        const double out = rhs.X + (2 * rhs.Y) + (3 * rhs.Z);
        if (std::isnan(in) && std::isnan(out)) return true;
        return in == out;
    }
    inline bool operator>(IXYZDouble &rhs) { return rhs < *this; }
    inline bool operator<=(IXYZDouble &rhs) { return !(*this > rhs); }
    inline bool operator>=(IXYZDouble &rhs) { return !(*this < rhs); }
} ImageXYZDouble;

typedef struct IRGBFloat {
    float R;
    float G;
    float B;
    mutable quint32 N = 1;
    mutable float A = 0.0;

    friend bool operator<(const IRGBFloat &lhs, const IRGBFloat &rhs) {
        const double in = lhs.R + (2 * lhs.G) + (3 * lhs.B);
        const double out = lhs.R + (2 * lhs.G) + (3 * lhs.B);
        if (std::isnan(in) || std::isnan(out)) return false;
        return in < out;
    }
    friend bool operator==(const IRGBFloat &lhs, const IRGBFloat &rhs) {
        const double in = lhs.R + (2 * lhs.G) + (3 * lhs.B);
        const double out = lhs.R + (2 * lhs.G) + (3 * lhs.B);
        if (std::isnan(in) && std::isnan(out)) return true;
        return in == out;
    }
    inline bool operator>(IRGBFloat &rhs) { return rhs < *this; }
    inline bool operator<=(IRGBFloat &rhs) { return !(*this > rhs); }
    inline bool operator>=(IRGBFloat &rhs) { return !(*this < rhs); }
} ImageRGBFloat;

typedef QPair<ImageXYZDouble, ImageRGBFloat> ColorPoint;

template<>
struct std::hash<ColorPoint>
{
    inline size_t operator()(const ColorPoint &lhs) const noexcept
    {
        double in = lhs.first.X + (2 * lhs.first.Y) + (3 * lhs.first.Z);
        if (std::isnan(in)) return 0;
        /*
         *    "You like playing it unsafe"
         *             "Do you?"
         *
         */
        return static_cast<size_t>(*reinterpret_cast<uint64_t *>(&in));
    }
};

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
