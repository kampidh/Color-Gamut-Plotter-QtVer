#ifndef PLOT_TYPEDEFS_H
#define PLOT_TYPEDEFS_H

#include "qdebug.h"
#include <QColor>
#include <QVector3D>
#include <cmath>

typedef struct IXYZDouble {
    double X;
    double Y;
    double Z;

    // crude hashing for quick pixel comparison
    friend bool operator<(const IXYZDouble &lhs, const IXYZDouble &rhs)
    {
        const double in = lhs.X + (2 * lhs.Y) + (3 * lhs.Z);
        const double out = rhs.X + (2 * rhs.Y) + (3 * rhs.Z);
        if (std::isnan(in) || std::isnan(out)) return false;
        return in < out;
    }
    friend bool operator==(const IXYZDouble &lhs, const IXYZDouble &rhs) {
        return (lhs.X == rhs.X) && (lhs.Y == rhs.Y) && (lhs.Z == rhs.Z);
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
        const double out = rhs.R + (2 * rhs.G) + (3 * rhs.B);
        if (std::isnan(in) || std::isnan(out)) return false;
        return in < out;
    }
    friend bool operator==(const IRGBFloat &lhs, const IRGBFloat &rhs) {
        return (lhs.R == rhs.R) && (lhs.G == rhs.G) && (lhs.B == rhs.B);
    }
    inline bool operator>(IRGBFloat &rhs) { return rhs < *this; }
    inline bool operator<=(IRGBFloat &rhs) { return !(*this > rhs); }
    inline bool operator>=(IRGBFloat &rhs) { return !(*this < rhs); }
} ImageRGBFloat;

template<typename T>
struct ImageRGBTyped {
    T R;
    T G;
    T B;
    mutable quint32 N = 1;
    mutable float A = 0.0;

    friend bool operator<(const ImageRGBTyped &lhs, const ImageRGBTyped &rhs) {
        if constexpr (std::numeric_limits<T>::is_integer) {
            const quint64 in = lhs.R + (2 * lhs.G) + (3 * lhs.B);
            const quint64 out = rhs.R + (2 * rhs.G) + (3 * rhs.B);
            if (std::isnan(in) || std::isnan(out)) return false;
            return in < out;
        } else {
            const double in = lhs.R + (2 * lhs.G) + (3 * lhs.B);
            const double out = rhs.R + (2 * rhs.G) + (3 * rhs.B);
            if (std::isnan(in) || std::isnan(out)) return false;
            return in < out;
        }
    }
    friend bool operator==(const ImageRGBTyped &lhs, const ImageRGBTyped &rhs) {
        return (lhs.R == rhs.R) && (lhs.G == rhs.G) && (lhs.B == rhs.B);
    }
    inline bool operator>(ImageRGBTyped &rhs) { return rhs < *this; }
    inline bool operator<=(ImageRGBTyped &rhs) { return !(*this > rhs); }
    inline bool operator>=(ImageRGBTyped &rhs) { return !(*this < rhs); }
};

typedef QPair<ImageXYZDouble, ImageRGBFloat> ColorPoint;
inline bool operator==(const ColorPoint &lhs, const ColorPoint &rhs) {
    return (lhs.first.X == rhs.first.X) && (lhs.first.Y == rhs.first.Y) && (lhs.first.Z == rhs.first.Z);
}
// inline bool operator<(const ColorPoint &lhs, const ColorPoint &rhs) {
//     return lhs.second.N < rhs.second.N;
// }
// inline bool operator>(const ColorPoint &lhs, const ColorPoint &rhs) { return rhs < lhs; }
// inline bool operator<=(const ColorPoint &lhs, const ColorPoint &rhs) { return !(lhs > rhs); }
// inline bool operator>=(const ColorPoint &lhs, const ColorPoint &rhs) { return !(lhs < rhs); }

template<>
struct std::hash<ColorPoint>
{
    inline size_t operator()(const ColorPoint &lhs) const noexcept
    {
        // double in = lhs.first.X + (2 * lhs.first.Y) + (3 * lhs.first.Z);
        // if (std::isnan(in)) return 0;
        // /*
        //  *    "You like playing it unsafe"
        //  *             "Do you?"
        //  *
        //  */
        // return static_cast<size_t>(*reinterpret_cast<uint64_t *>(&in));

        // Thank you SO 664014 & 20511347 for the quick vector hashing function...
        // This one needed to generate key for unordered_set for fast pixel sorting
        // during trimming duplicate pixels.

        const float r = lhs.first.X;
        const float g = lhs.first.Y;
        const float b = lhs.first.Z;

        std::vector<uint32_t> vec{*reinterpret_cast<const uint32_t *>(&r),
                                  *reinterpret_cast<const uint32_t *>(&g),
                                  *reinterpret_cast<const uint32_t *>(&b)};
        std::size_t seed = vec.size();
        for(auto x : vec) {
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

template<>
struct std::hash<ImageRGBFloat>
{
    inline size_t operator()(const ImageRGBFloat &lhs) const noexcept
    {
        // double in = lhs.R + (2 * lhs.G) + (3 * lhs.B);
        // return static_cast<size_t>(*reinterpret_cast<uint64_t *>(&in));

        const float r = lhs.R;
        const float g = lhs.G;
        const float b = lhs.B;

        std::vector<uint32_t> vec{*reinterpret_cast<const uint32_t *>(&r),
                                  *reinterpret_cast<const uint32_t *>(&g),
                                  *reinterpret_cast<const uint32_t *>(&b)};
        std::size_t seed = vec.size();
        for(auto x : vec) {
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

template<>
struct std::hash<ImageXYZDouble>
{
    inline size_t operator()(const ImageXYZDouble &lhs) const noexcept
    {
        double in = lhs.X + (2 * lhs.Y) + (3 * lhs.Z);
        return static_cast<size_t>(*reinterpret_cast<uint64_t *>(&in));
    }
};

template<typename T>
struct std::hash<ImageRGBTyped<T>>
{
    inline size_t operator()(const ImageRGBTyped<T> &lhs) const noexcept
    {
        if constexpr (std::numeric_limits<T>::is_integer) {
            std::vector<uint32_t>vec{lhs.R, lhs.G, lhs.B};
            std::size_t seed = vec.size();
            for(auto x : vec) {
                x = ((x >> 16) ^ x) * 0x45d9f3b;
                x = ((x >> 16) ^ x) * 0x45d9f3b;
                x = (x >> 16) ^ x;
                seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        } else {
            const float r = lhs.R;
            const float g = lhs.G;
            const float b = lhs.B;

            std::vector<uint32_t> vec{*reinterpret_cast<const uint32_t *>(&r),
                                      *reinterpret_cast<const uint32_t *>(&g),
                                      *reinterpret_cast<const uint32_t *>(&b)};
            std::size_t seed = vec.size();
            for(auto x : vec) {
                x = ((x >> 16) ^ x) * 0x45d9f3b;
                x = ((x >> 16) ^ x) * 0x45d9f3b;
                x = (x >> 16) ^ x;
                seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    }
};

struct PlotSetting2D {
    bool enableAA{false};
    bool forceBucket{false};
    bool use16Bit{false};
    bool showStatistics{false};
    bool showGridsAndSpectrum{false};
    bool showsRGBGamut{false};
    bool showImageGamut{false};
    bool showMacAdamEllipses{false};
    bool showColorCheckerPoints{false};
    bool showBlBodyLocus{false};
    double particleOpacity{0.0};
    int particleSize{0};
    double renderScale{0.0};
    int multisample3d{0};
};

struct PlotSetting3D {
    bool useMaxBlend{false};
    bool toggleOpaque{false};
    bool useVariableSize{false};
    bool useSmoothParticle{true};
    bool useMonochrome{false};
    bool useOrtho{true};
    int axisModeInt{6};
    int modeInt{-1};
    int ccModeInt{-1};
    float minAlpha{0.1};
    float particleSize{1.0};
    float turntableAngle{0.0};
    float camDistToTarget{1.3};
    float fov{45.0};
    float yawAngle{180.0};
    float pitchAngle{90.0};
    QVector3D targetPos{0.0, 0.0, 0.5};
    QColor monoColor{255, 255, 255};
    QColor bgColor{16, 16, 16};

    PlotSetting3D& operator=(const PlotSetting3D &rhs) {
        if (this == &rhs)
            return *this;

        useMaxBlend = rhs.useMaxBlend;
        toggleOpaque = rhs.toggleOpaque;
        useVariableSize = rhs.useVariableSize;
        useSmoothParticle = rhs.useSmoothParticle;
        useMonochrome = rhs.useMonochrome;
        useOrtho = rhs.useOrtho;
        axisModeInt = rhs.axisModeInt;
        modeInt = rhs.modeInt;
        ccModeInt = rhs.ccModeInt;
        minAlpha = std::max(0.0f, std::min(1.0f, rhs.minAlpha));
        particleSize = std::max(0.0f, std::min(20.0f, rhs.particleSize));
        turntableAngle = std::max(0.0f, std::min(360.0f, rhs.turntableAngle));
        camDistToTarget = std::max(0.005f, rhs.camDistToTarget);
        fov = std::max(1.0f, std::min(170.0f, rhs.fov));
        yawAngle = std::max(0.0f, std::min(360.0f, rhs.yawAngle));
        pitchAngle = std::max(-90.0f, std::min(90.0f, rhs.pitchAngle));
        targetPos = rhs.targetPos;
        monoColor = rhs.monoColor;
        bgColor = rhs.bgColor;

        return *this;
    };
};

#endif // PLOT_TYPEDEFS_H
