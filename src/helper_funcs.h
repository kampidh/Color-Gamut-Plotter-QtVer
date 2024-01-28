#ifndef HELPER_FUNCS_H
#define HELPER_FUNCS_H

#include <cmath>
#include <future>
#include <algorithm>
#include <QVector>
#include <QVector3D>
#include <QGenericMatrix>

#include <lcms2.h>

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

inline QVector3D xyyToXyz(QVector3D inst)
{
    QVector3D iXYZ;
    if (inst.y() != 0) {
        float X = (inst.x() * inst.z()) / inst.y();
        float Y = inst.z();
        float Z = ((1.0 - inst.x() - inst.y()) * inst.z()) / inst.y();
        iXYZ = QVector3D(X, Y, Z);
    } else {
        iXYZ = QVector3D(0, 0, 0);
    }
    return iXYZ;
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
    return QVector3D{u, vr, L / 100.0f};
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
        oLuv = QVector3D(ur, vr * (2.0f / 3.0f), iXYZ.y());
    } else if (mode == UCS_1976_LUV) {
        // UCS 1976 L'u'v'
        oLuv = QVector3D(ur, vr, L / 100.0f);
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
    // cmsSetAdaptationState(0.0);

    cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();
    cmsHPROFILE hsXYZ = cmsCreateXYZProfile();
    cmsHTRANSFORM srgbtoxyz =
        cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);

    QVector<QVector3D> outGamut;

    const int sampleNum = 100;

    // R - Y
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(i) / static_cast<double>(sampleNum);

        const double rgb[3] = {1.0, secd, 0.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(srgbtoxyz, &rgb, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        const QVector3D output{static_cast<float>(bufxyY.x),
                               static_cast<float>(bufxyY.y),
                               static_cast<float>(bufxyY.Y)};
        outGamut.append(output);
    }

    // Y - G
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(sampleNum - i) / static_cast<double>(sampleNum);

        const double rgb[3] = {secd, 1.0, 0.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(srgbtoxyz, &rgb, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        const QVector3D output{static_cast<float>(bufxyY.x),
                               static_cast<float>(bufxyY.y),
                               static_cast<float>(bufxyY.Y)};
        outGamut.append(output);
    }

    // G - C
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(i) / static_cast<double>(sampleNum);

        const double rgb[3] = {0.0, 1.0, secd};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(srgbtoxyz, &rgb, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        const QVector3D output{static_cast<float>(bufxyY.x),
                               static_cast<float>(bufxyY.y),
                               static_cast<float>(bufxyY.Y)};
        outGamut.append(output);
    }

    // C - B
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(sampleNum - i) / static_cast<double>(sampleNum);

        const double rgb[3] = {0.0, secd, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(srgbtoxyz, &rgb, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        const QVector3D output{static_cast<float>(bufxyY.x),
                               static_cast<float>(bufxyY.y),
                               static_cast<float>(bufxyY.Y)};
        outGamut.append(output);
    }

    // B - M
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(i) / static_cast<double>(sampleNum);

        const double rgb[3] = {secd, 0.0, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(srgbtoxyz, &rgb, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        const QVector3D output{static_cast<float>(bufxyY.x),
                               static_cast<float>(bufxyY.y),
                               static_cast<float>(bufxyY.Y)};
        outGamut.append(output);
    }

    // M - R
    for (int i = 0; i < sampleNum; i++) {
        const double secd = static_cast<double>(sampleNum - i) / static_cast<double>(sampleNum);

        const double rgb[3] = {1.0, 0.0, secd};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(srgbtoxyz, &rgb, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        const QVector3D output{static_cast<float>(bufxyY.x),
                               static_cast<float>(bufxyY.y),
                               static_cast<float>(bufxyY.Y)};
        outGamut.append(output);
    }

    cmsDeleteTransform(srgbtoxyz);
    cmsCloseProfile(hsRGB);
    cmsCloseProfile(hsXYZ);

    return outGamut;
}

#endif // HELPER_FUNCS_H
