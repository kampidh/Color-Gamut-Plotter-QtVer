#ifndef HELPER_FUNCS_H
#define HELPER_FUNCS_H

#include <cmath>
#include <future>
#include <algorithm>
#include <QVector>
#include <QVector3D>
#include <QGenericMatrix>

// thank you https://codereview.stackexchange.com/questions/22744/multi-threaded-sort
template<class T>
void parallel_sort(T *data, int len, int grainsize, const float *compobject)
{
    if constexpr (!std::numeric_limits<T>::is_integer) {
        return;
    }
    if (len < grainsize) {
        std::sort(data, data + len, [&](const T &lhs, const T &rhs) {
            // NaN check
            if (std::isnan(compobject[lhs])) return false;
            if (std::isnan(compobject[rhs])) return true;
            return compobject[lhs] > compobject[rhs];
        });
    } else {
        auto future = std::async(parallel_sort<T>, data, len / 2, grainsize, compobject);

        parallel_sort(data + len / 2, len - len / 2, grainsize, compobject);

        future.wait();

        std::inplace_merge(data, data + len / 2, data + len, [&](const T &lhs, const T &rhs) {
            // NaN check
            if (std::isnan(compobject[lhs])) return false;
            if (std::isnan(compobject[rhs])) return true;
            return compobject[lhs] > compobject[rhs];
        });
    }
}

static constexpr float xyz2srgb[9] = {3.2404542, -1.5371385, -0.4985314,
                                      -0.9692660, 1.8760108, 0.0415560,
                                      0.0556434, -0.2040259, 1.0572252};

static constexpr float srgb2xyzD65[9] = {0.4124564, 0.3575761, 0.1804375,
                                         0.2126729, 0.7151522, 0.0721750,
                                         0.0193339, 0.1191920, 0.9503041};

static constexpr float D502D65Bradford[9] = {0.9555766, -0.0230393, 0.0631636,
                                             -0.0282895, 1.0099416, 0.0210077,
                                             0.0122982, -0.0204830, 1.3299098};

static constexpr float D652D50Bradford[9] = {0.9857398, 0.0000000, 0.0000000,
                                             0.0000000, 1.0000000, 0.0000000,
                                             0.0000000, 0.0000000, 1.4861429};

static constexpr float D65WPxyy[3] = {0.3127, 0.3290, 1.0000};

inline QVector3D xyyToXyz(const QVector3D &v)
{
    QVector3D iXYZ;
    if (v.y() != 0) {
        float X = (v.x() * v.z()) / v.y();
        float Y = v.z();
        float Z = ((1.0 - v.x() - v.y()) * v.z()) / v.y();
        iXYZ = QVector3D(X, Y, Z);
    } else {
        iXYZ = QVector3D(0, 0, 0);
    }
    return iXYZ;
}

inline QVector3D xyzToXyy(const QVector3D &v, const QVector3D &whiteXyy = QVector3D{0.3127, 0.3290, 1.0000})
{
    if (v.x() == 0 && v.y() == 0 && v.z() == 0) {
        return QVector3D{whiteXyy.x(), whiteXyy.y(), 0.0f};
    }

    const float x = v.x() / (v.x() + v.y() + v.z());
    const float y = v.y() / (v.x() + v.y() + v.z());
    const float Y = v.y();

    return QVector3D{x, y, Y};
}

inline float fromSrgb(const float &v)
{
    if (v > 0.04045f) {
        return (float)(pow((v + 0.055f) / 1.055f, 2.4f));
    } else {
        return (float)(v / 12.92f);
    }
}

inline float toSrgb(const float &v)
{
    if (v > 0.0031308f) {
        return (float)((1.055f * pow(v, 1.0f / 2.4f)) - 0.055f);
    } else {
        return (float)(12.92f * v);
    }
}

inline QVector3D fromSrgb(const QVector3D &v)
{
    return QVector3D{fromSrgb(v.x()), fromSrgb(v.y()), fromSrgb(v.z())};
}

inline QVector3D toSrgb(const QVector3D &v)
{
    return QVector3D{toSrgb(v.x()), toSrgb(v.y()), toSrgb(v.z())};
}

QVector3D srgbToXyz(const QVector3D &v)
{
    const QGenericMatrix<3, 3, float> srgbxyz(srgb2xyzD65);

    const float iRGBDummy[3] = {fromSrgb(v.x()), fromSrgb(v.y()), fromSrgb(v.z())};
    const QGenericMatrix<1, 3, float> iXYZMat(iRGBDummy);

    const QGenericMatrix<1, 3, float> xyzDummy = srgbxyz * iXYZMat;
    const QVector3D xyz(xyzDummy.constData()[0], xyzDummy.constData()[1], xyzDummy.constData()[2]);

    return xyz;
}

QVector3D srgbToXyy(const QVector3D &v)
{
    const QGenericMatrix<3, 3, float> srgbxyz(srgb2xyzD65);

    const float iRGBDummy[3] = {fromSrgb(v.x()), fromSrgb(v.y()), fromSrgb(v.z())};
    const QGenericMatrix<1, 3, float> iXYZMat(iRGBDummy);

    const QGenericMatrix<1, 3, float> xyzDummy = srgbxyz * iXYZMat;
    const QVector3D xyz(xyzDummy.constData()[0], xyzDummy.constData()[1], xyzDummy.constData()[2]);

    return xyzToXyy(xyz);
}

// 1960 UCS uv
inline QVector3D xyToUv(const QVector3D &xyy)
{
    const float u = (4.0f * xyy.x()) / ((12.0f * xyy.y()) - (2.0f * xyy.x()) + 3.0f);
    const float v = (6.0f * xyy.y()) / ((12.0f * xyy.y()) - (2.0f * xyy.x()) + 3.0f);
    return QVector3D{u, v, xyy.z()};
}

// 1976 UCS u'v'
inline QVector3D xyToUrvr(const QVector3D &xyy, const QVector3D &wxyy)
{
    const QVector3D iXYZ = xyyToXyz(xyy);
    const QVector3D wXYZ = xyyToXyz(wxyy);

    const float e = 0.008856f;
    const float k = 903.3f;

    const float yt = iXYZ.y() / wXYZ.y();

    [[maybe_unused]]
    const float L = [&]() {
        if (yt > e) {
            return static_cast<float>((116.0f * pow(yt, 1.0f / 3.0f)) - 16.0f);
        } else {
            return static_cast<float>(k * yt);
        }
    }();

    const float u = (4.0f * xyy.x()) / ((12.0f * xyy.y()) - (2.0f * xyy.x()) + 3.0f);
    const float v = (6.0f * xyy.y()) / ((12.0f * xyy.y()) - (2.0f * xyy.x()) + 3.0f);
    const float vr = (3.0f / 2.0f) * v;
    return QVector3D{u, vr, xyy.z()};
}

enum UcsModes {
    UCS_1960_YUV,
    UCS_1976_LUV,
    UCS_1976_LUV_STAR
};

QVector3D xyyToUCS(const QVector3D &xyy, const QVector3D &wxyy, const UcsModes &mode)
{
    // CIE Luv / Lu'v'
    const float e = 0.008856f;
    const float k = 903.3f;

    const QVector3D iXYZ = xyyToXyz(xyy);
    const QVector3D wXYZ = xyyToXyz(wxyy);

    const float yt = iXYZ.y() / wXYZ.y();

    const float ur = (4.0f * iXYZ.x()) / (iXYZ.x() + (15.0f * iXYZ.y()) + (3.0f * iXYZ.z()));
    const float vr = (9.0f * iXYZ.y()) / (iXYZ.x() + (15.0f * iXYZ.y()) + (3.0f * iXYZ.z()));

    const float urt = (4.0f * wXYZ.x()) / (wXYZ.x() + (15.0f * wXYZ.y()) + (3.0f * wXYZ.z()));
    const float vrt = (9.0f * wXYZ.y()) / (wXYZ.x() + (15.0f * wXYZ.y()) + (3.0f * wXYZ.z()));

    float L;

    if (yt > e) {
        L = (116.0f * pow(yt, 1.0f / 3.0f)) - 16.0f;
    } else {
        L = k * yt;
    }

    const float u = 13.0f * L * (ur - urt);
    const float v = 13.0f * L * (vr - vrt);

    QVector3D oLuv;
    if (mode == UCS_1960_YUV) {
        // UCS 1960 uv
        // "The CIE 1960 UCS does not define a luminance or lightness component"
        // - Wikipedia on CIE 1960 color space
        // errrr.... let's use Y as 1931 does, Yuv it is.
        oLuv = QVector3D(ur, vr * (2.0f / 3.0f), xyy.z());
    } else if (mode == UCS_1976_LUV) {
        // UCS 1976 Yu'v'
        oLuv = QVector3D(ur, vr, xyy.z());
    } else if (mode == UCS_1976_LUV_STAR) {
        // CIE 1976 L*u*v*
        oLuv = QVector3D(u, v, L) / 100.0f;
    }

    return oLuv;
}

QVector3D xyyToLab(const QVector3D &xyy, const QVector3D &wxyy)
{
    const float e = 0.008856f;
    const float k = 903.3f;

    const QVector3D iXYZ = xyyToXyz(xyy);
    const QVector3D wXYZ = xyyToXyz(wxyy);

    const float xr = iXYZ.x() / wXYZ.x();
    const float yr = iXYZ.y() / wXYZ.y();
    const float zr = iXYZ.z() / wXYZ.z();

    float fx, fy, fz;

    if (xr > e) {
        fx = pow(xr, 1.0f / 3.0f);
    } else {
        fx = ((k * xr) + 16.0f) / 116.0f;
    }

    if (yr > e) {
        fy = pow(yr, 1.0f / 3.0f);
    } else {
        fy = ((k * yr) + 16.0f) / 116.0f;
    }

    if (zr > e) {
        fz = pow(zr, 1.0f / 3.0f);
    } else {
        fz = ((k * zr) + 16.0f) / 116.0f;
    }

    float L = (116.0f * fy) - 16.0f;
    float a = 500.0f * (fx - fy);
    float b = 200.0f * (fy - fz);

    QVector3D oLab = QVector3D(a, b, L) / 100.0f;

    return oLab;
}

QVector3D xyyToOklab(const QVector3D &xyy, const QVector3D &wxyy)
{
    const QVector3D iXYZ = xyyToXyz(xyy);
    [[maybe_unused]]
    const QVector3D wXYZ = xyyToXyz(wxyy);

    const float m1Mat[9] = {
        0.8189330101, 0.3618667424, -0.1288597137,
        0.0329845436, 0.9293118715, 0.0361456387,
        0.0482003018, 0.2643662691, 0.6338517070
    };

    const float m2Mat[9] = {
        0.2104542553, 0.7936177850, -0.0040720468,
        1.9779984951, -2.4285922050, 0.4505937099,
        0.0259040371, 0.7827717662, -0.8086757660
    };

    const QGenericMatrix<3, 3, float> oklM1(m1Mat);
    const QGenericMatrix<3, 3, float> oklM2(m2Mat);

    const float iXYZDummy[3] = {iXYZ.x(), iXYZ.y(), iXYZ.z()};
    const QGenericMatrix<1, 3, float> iXYZMat(iXYZDummy);

    const QGenericMatrix<1, 3, float> lmsDummy = oklM1 * iXYZMat;
    const QVector3D lms(lmsDummy.constData()[0], lmsDummy.constData()[1], lmsDummy.constData()[2]);

    float lmsL, lmsM, lmsS;
    // I'm not sure how oklab handle negative values...
    if (lms.x() > 0) {
        lmsL = std::cbrt(lms.x());
    } else {
        lmsL = 0.0;
    }
    if (lms.y() > 0) {
        lmsM =  std::cbrt(lms.y());
    } else {
        lmsM = 0.0;
    }
    if (lms.z() > 0) {
        lmsS =  std::cbrt(lms.z());
    } else {
        lmsS = 0.0;
    }
    const float lmsaDum[3] = {lmsL, lmsM, lmsS};

    const QGenericMatrix<1, 3, float> oLabDummy = oklM2 * QGenericMatrix<1, 3, float>(lmsaDum);

    const QVector3D oLab(oLabDummy.constData()[1], oLabDummy.constData()[2], oLabDummy.constData()[0]);
    return oLab;
}

QVector<QVector3D> getSrgbGamutxyy()
{
    QVector<QVector3D> outGamut;

    const int sampleNum = 100;

    // R - Y
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(i) / static_cast<double>(sampleNum);
        const double rgb[3] = {1.0, secd, 0.0};
        const QVector3D output =
            srgbToXyy(QVector3D{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])});
        outGamut.append(output);
    }

    // Y - G
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(sampleNum - i) / static_cast<double>(sampleNum);
        const double rgb[3] = {secd, 1.0, 0.0};
        const QVector3D output =
            srgbToXyy(QVector3D{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])});
        outGamut.append(output);
    }

    // G - C
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(i) / static_cast<double>(sampleNum);
        const double rgb[3] = {0.0, 1.0, secd};
        const QVector3D output =
            srgbToXyy(QVector3D{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])});
        outGamut.append(output);
    }

    // C - B
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(sampleNum - i) / static_cast<double>(sampleNum);
        const double rgb[3] = {0.0, secd, 1.0};
        const QVector3D output =
            srgbToXyy(QVector3D{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])});
        outGamut.append(output);
    }

    // B - M
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(i) / static_cast<double>(sampleNum);
        const double rgb[3] = {secd, 0.0, 1.0};
        const QVector3D output =
            srgbToXyy(QVector3D{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])});
        outGamut.append(output);
    }

    // M - R
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(sampleNum - i) / static_cast<double>(sampleNum);
        const double rgb[3] = {1.0, 0.0, secd};
        const QVector3D output =
            srgbToXyy(QVector3D{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])});
        outGamut.append(output);
    }

    return outGamut;
}

#endif // HELPER_FUNCS_H
