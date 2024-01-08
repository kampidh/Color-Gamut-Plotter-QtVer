/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "imageparsersc.h"
#include "global_variables.h"

#include <QVector3D>

#include <QDebug>

#include <QApplication>
#include <QColorSpace>
#include <QImage>
#include <QProgressDialog>

#include <QFuture>
#include <QtConcurrent>
#include <QGenericMatrix>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

#include <lcms2.h>

inline float lerp(float a, float b, float f)
{
    return a * (1.0 - f) + (b * f);
}

template<typename T, typename std::enable_if_t<!std::numeric_limits<T>::is_integer, int> = 1>
inline double value(const T src)
{
    double dst = double(src);
    if (ClampNegative) dst = std::max(0.0, dst);
    if (ClampPositive) dst = std::min(1.0, dst);
    return dst;
}

template<typename T, typename std::enable_if_t<std::numeric_limits<T>::is_integer, int> = 1>
inline double value(const T src)
{
    double dst =  double(std::numeric_limits<T>::max());
    if (ClampNegative) dst = std::max(0.0, dst);
    if (ClampPositive) dst = std::min(1.0, dst);
    return dst;
}

const static float adaptationState = 0.0;
const static cmsUInt32Number displayPreviewIntent = INTENT_RELATIVE_COLORIMETRIC;

class Q_DECL_HIDDEN ImageParserSC::Private
{
public:
    QSize m_dimension{};
    QString m_profileName{};
    QVector<ImageXYZDouble> m_outerGamut{};
    QVector3D m_prfWtpt{};

    QByteArray m_rawProfile;

    quint8 chR{0};
    quint8 chG{1};
    quint8 chB{2};

    const quint8 *m_rawImageByte;

    QVector<ColorPoint> *m_outCp{nullptr};

    bool m_isSrgb{false};
    bool hasColorants{false};
    bool alreadyTrimmed{false};

    cmsCIEXYZTRIPLE colorants;
    cmsCIEXYZ p_wtptXYZ{0,0,0};
};

ImageParserSC::ImageParserSC()
    : d(new Private)
{
    // moved into inputFile()
}

ImageParserSC::~ImageParserSC()
{
    delete d;
}

void ImageParserSC::inputFile(const QImage &imgIn, int size, QVector<ColorPoint> *outCp)
{
    const QByteArray imRawIcc = imgIn.colorSpace().iccProfile();
    const int imLongSide = std::max(imgIn.width(), imgIn.height());

    d->m_rawImageByte = imgIn.constBits();

    d->m_dimension = QSize(imgIn.size());

    // Do not adapt to illuminant on Absolute Colorimetric
    cmsSetAdaptationState(adaptationState);

    const cmsHPROFILE hsIMG = cmsOpenProfileFromMem(imRawIcc.data(), imRawIcc.size());
    const cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();

    /*
     * Reserved in case I need linear / scRGB
     * maybe Qt 6+ where there will be Float QImage?
    cmsToneCurve *linTRC = cmsBuildGamma(NULL, 1.0);
    cmsToneCurve *linrgb[3]{linTRC, linTRC, linTRC};
    const cmsCIExyY sRgbD65 = {0.3127, 0.3290, 1.0000};
    const cmsCIExyYTRIPLE srgbPrim = {{0.6400, 0.3300, 0.2126},
                                      {0.3000, 0.6000, 0.7152},
                                      {0.1500, 0.0600, 0.0722}};
    const cmsHPROFILE hLinsRGB = cmsCreateRGBProfile(&sRgbD65, &srgbPrim, linrgb);
    */

    const cmsHPROFILE hsXYZ = cmsCreateXYZProfile();

    if (hsIMG) {
        d->m_rawProfile = imRawIcc;
        cmsUInt32Number tmpSize = 0;
        tmpSize = cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
        wchar_t *tmp = new wchar_t[tmpSize];
        cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, tmp, tmpSize);
        d->m_profileName = QString::fromWCharArray(tmp);
        delete[] tmp;

        // cherrypick from krita

        cmsCIEXYZ mediaWhitePoint{0,0,0};
        cmsCIExyY whitePoint{0,0,0};

        cmsCIEXYZ baseMediaWhitePoint;//dummy to hold copy of mediawhitepoint if this is modified by chromatic adaption.
        cmsCIEXYZ *mediaWhitePointPtr;
        bool whiteComp[3];
        bool whiteIsD50;
        // Possible bug in profiles: there are in fact some that says they contain that tag
        //    but in fact the pointer is null.
        //    Let's not crash on it anyway, and assume there is no white point instead.
        //    BUG:423685
        if (cmsIsTag(hsIMG, cmsSigMediaWhitePointTag)
            && (mediaWhitePointPtr = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigMediaWhitePointTag))) {

            mediaWhitePoint = *(mediaWhitePointPtr);
            baseMediaWhitePoint = mediaWhitePoint;

            whiteComp[0] = std::fabs(baseMediaWhitePoint.X - cmsD50_XYZ()->X) < 0.0001;
            whiteComp[1] = std::fabs(baseMediaWhitePoint.Y - cmsD50_XYZ()->Y) < 0.0001;
            whiteComp[2] = std::fabs(baseMediaWhitePoint.Z - cmsD50_XYZ()->Z) < 0.0001;
            whiteIsD50 = std::all_of(std::begin(whiteComp), std::end(whiteComp), [](bool b) {return b;});

            cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
            cmsCIEXYZ *CAM1;
            if (cmsIsTag(hsIMG, cmsSigChromaticAdaptationTag)
                && (CAM1 = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigChromaticAdaptationTag))
                && whiteIsD50) {
                //the chromatic adaption tag represent a matrix from the actual white point of the profile to D50.

                       //We first put all our data into structures we can manipulate.
                double d3dummy [3] = {mediaWhitePoint.X, mediaWhitePoint.Y, mediaWhitePoint.Z};
                QGenericMatrix<1, 3, double> whitePointMatrix(d3dummy);
                QTransform invertDummy(CAM1[0].X, CAM1[0].Y, CAM1[0].Z, CAM1[1].X, CAM1[1].Y, CAM1[1].Z, CAM1[2].X, CAM1[2].Y, CAM1[2].Z);
                //we then abuse QTransform's invert function because it probably does matrix inversion 20 times better than I can program.
                //if the matrix is uninvertable, invertedDummy will be an identity matrix, which for us means that it won't give any noticeable
                //effect when we start multiplying.
                QTransform invertedDummy = invertDummy.inverted();
                //we then put the QTransform into a generic 3x3 matrix.
                double d9dummy [9] = {invertedDummy.m11(), invertedDummy.m12(), invertedDummy.m13(),
                    invertedDummy.m21(), invertedDummy.m22(), invertedDummy.m23(),
                    invertedDummy.m31(), invertedDummy.m32(), invertedDummy.m33()
                };
                QGenericMatrix<3, 3, double> chromaticAdaptionMatrix(d9dummy);
                //multiplying our inverted adaption matrix with the whitepoint gives us the right whitepoint.
                QGenericMatrix<1, 3, double> result = chromaticAdaptionMatrix * whitePointMatrix;
                //and then we pour the matrix into the whitepoint variable. Generic matrix does row/column for indices even though it
                //uses column/row for initialising.
                mediaWhitePoint.X = result(0, 0);
                mediaWhitePoint.Y = result(1, 0);
                mediaWhitePoint.Z = result(2, 0);
                cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
            }
        }

               // in case missing whitepoint tag
        if (mediaWhitePoint.X == 0 && mediaWhitePoint.Y == 0 && mediaWhitePoint.Z == 0) {
            mediaWhitePoint = *cmsD50_XYZ();
            cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
        }

        d->p_wtptXYZ = mediaWhitePoint;

        cmsCIEXYZ *tempColorantsRed, *tempColorantsGreen, *tempColorantsBlue;
        // Note: don't assume that cmsIsTag is enough to check for errors; check the pointers, too
        // BUG:423685
        if (cmsIsTag(hsIMG, cmsSigRedColorantTag) && cmsIsTag(hsIMG, cmsSigRedColorantTag) && cmsIsTag(hsIMG, cmsSigRedColorantTag)
            && (tempColorantsRed = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigRedColorantTag))
            && (tempColorantsGreen = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigGreenColorantTag))
            && (tempColorantsBlue = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigBlueColorantTag))) {
            cmsCIEXYZTRIPLE tempColorants;
            tempColorants.Red = *tempColorantsRed;
            tempColorants.Green = *tempColorantsGreen;
            tempColorants.Blue = *tempColorantsBlue;

            cmsCIEXYZTRIPLE colorants;

                   //convert to d65, this is useless.
            cmsAdaptToIlluminant(&colorants.Red, cmsD50_XYZ(), &mediaWhitePoint, &tempColorants.Red);
            cmsAdaptToIlluminant(&colorants.Green, cmsD50_XYZ(), &mediaWhitePoint, &tempColorants.Green);
            cmsAdaptToIlluminant(&colorants.Blue, cmsD50_XYZ(), &mediaWhitePoint, &tempColorants.Blue);

            d->colorants = colorants;
            d->hasColorants = true;
        } else {
            d->hasColorants = false;
        }
    }

    const cmsHTRANSFORM srgbtoxyz =
        cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
    const cmsHTRANSFORM imgtoxyz = [&]() {
        if (hsIMG) {
            return cmsCreateTransform(hsIMG, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
        } else {
            return cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
        }
    }();

    const cmsHTRANSFORM xyztosrgb = cmsCreateTransform(hsXYZ, TYPE_XYZ_DBL, hsRGB, TYPE_RGB_FLT, displayPreviewIntent, 0);

    const quint8 chNum = 4;

    const int longestSize = std::max(d->m_dimension.width(), d->m_dimension.height());
    const int resizedLongSizeRatio = std::ceil((longestSize * 1.0) / (size * 1.0));
    const int resizedMaxPixelCount =
        (d->m_dimension.width() / resizedLongSizeRatio) * (d->m_dimension.height() / resizedLongSizeRatio);

    const int imgPixelCount = d->m_dimension.width() * d->m_dimension.height();

    const int resizeFactor = std::ceil((imgPixelCount * 1.0) / (resizedMaxPixelCount * 1.0));
    const int pixelSize = chNum * resizeFactor;

    d->m_outCp = outCp;

    if (imgIn.pixelFormat().colorModel() == QPixelFormat::RGB) {
        d->chR = 0;
        d->chG = 1;
        d->chB = 2;
    } else if (imgIn.pixelFormat().colorModel() == QPixelFormat::BGR) {
        d->chR = 2;
        d->chG = 1;
        d->chB = 0;
    }

    const auto imFmt = imgIn.format();
    switch (imFmt) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied: {

        // 32 bit is always BGR?
        d->chR = 2;
        d->chG = 1;
        d->chB = 0;

        QVector<const quint8 *> imgPointers;

        const auto *dataRaw = reinterpret_cast<const quint8 *>(d->m_rawImageByte);
        for (int i = 0; i < imgIn.sizeInBytes() / sizeof(quint8); i += pixelSize) {
            imgPointers.append(reinterpret_cast<const quint8 *>(dataRaw + i));
        }

        calculateFromRaw<quint8>(imgPointers, srgbtoxyz, imgtoxyz, xyztosrgb);
    } break;

    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied: {
        QVector<const quint8 *> imgPointers;

        const auto *dataRaw = reinterpret_cast<const quint16 *>(d->m_rawImageByte);
        for (int i = 0; i < imgIn.sizeInBytes() / sizeof(quint16); i += pixelSize) {
            imgPointers.append(reinterpret_cast<const quint8 *>(dataRaw + i));
        }

        calculateFromRaw<quint16>(imgPointers, srgbtoxyz, imgtoxyz, xyztosrgb);
    } break;

    default: {
        QByteArray imDat;

        {
            const QImage img = [&]() {
                if (imLongSide > size) {
                    return imgIn.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation);
                }
                return imgIn;
            }();

            QProgressDialog pDial;
            pDial.setModal(true);
            pDial.setMinimum(0);
            pDial.setMaximum(img.width() * img.height());
            pDial.setLabelText("Resizing...");
            pDial.setCancelButtonText("Stop");

            pDial.show();
            QGuiApplication::processEvents();

            quint64 it = 0;

            for (int i = 0; i < img.width(); i++) {
                for (int j = 0; j < img.height(); j++) {
                    const float imm[3]{static_cast<float>(img.pixelColor(i, j).redF()),
                                       static_cast<float>(img.pixelColor(i, j).greenF()),
                                       static_cast<float>(img.pixelColor(i, j).blueF())};
                    const QByteArray arr = QByteArray::fromRawData(reinterpret_cast<const char*>(imm), sizeof(float) * 3);
                    imDat.append(arr);
                    if (it % 5000000 == 0) {
                        if (pDial.wasCanceled()) break;
                        pDial.setValue(it);
                        QGuiApplication::processEvents();
                    }
                    it++;
                }
            }

            pDial.close();
        }

        qDebug() << "Buffer size:" << imDat.size();

        QVector<const quint8 *> imgPointers;

        const auto *dataRaw = reinterpret_cast<const float *>(imDat.constData());
        for (int i = 0; i < imDat.size() / sizeof(float); i += 3) {
            imgPointers.append(reinterpret_cast<const quint8 *>(dataRaw + i));
        }

        calculateFromRaw<float>(imgPointers, srgbtoxyz, imgtoxyz, xyztosrgb);
    } break;
    }

    cmsDeleteTransform(srgbtoxyz);
    cmsDeleteTransform(imgtoxyz);
    cmsDeleteTransform(xyztosrgb);
}

void ImageParserSC::inputFile(const QByteArray &rawData,
                              const QByteArray &iccData,
                              ImageColorDepthID depthId,
                              QSize imgSize,
                              int size,
                              QVector<ColorPoint> *outCp)
{
    if (depthId != Float32BitsColorDepthID) {
        qDebug() << "warning, depth is not float";
    }

    d->m_rawImageByte = reinterpret_cast<const quint8 *>(rawData.constData());

    cmsSetAdaptationState(adaptationState);

    const cmsHPROFILE hsIMG = cmsOpenProfileFromMem(iccData.data(), iccData.size());
    const cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();

    /*
     * Reserved in case I need linear / scRGB
     * maybe Qt 6+ where there will be Float QImage?
    cmsToneCurve *linTRC = cmsBuildGamma(NULL, 1.0);
    cmsToneCurve *linrgb[3]{linTRC, linTRC, linTRC};
    const cmsCIExyY sRgbD65 = {0.3127, 0.3290, 1.0000};
    const cmsCIExyYTRIPLE srgbPrim = {{0.6400, 0.3300, 0.2126},
                                      {0.3000, 0.6000, 0.7152},
                                      {0.1500, 0.0600, 0.0722}};
    const cmsHPROFILE hLinsRGB = cmsCreateRGBProfile(&sRgbD65, &srgbPrim, linrgb);
    */

    const cmsHPROFILE hsXYZ = cmsCreateXYZProfile();

    if (hsIMG) {
        d->m_rawProfile = iccData;
        cmsUInt32Number tmpSize = 0;
        tmpSize = cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
        wchar_t *tmp = new wchar_t[tmpSize];
        cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, tmp, tmpSize);
        d->m_profileName = QString::fromWCharArray(tmp);
        delete[] tmp;

        // cherrypick from krita

        cmsCIEXYZ mediaWhitePoint{0,0,0};
        cmsCIExyY whitePoint{0,0,0};

        cmsCIEXYZ baseMediaWhitePoint;//dummy to hold copy of mediawhitepoint if this is modified by chromatic adaption.
        cmsCIEXYZ *mediaWhitePointPtr;
        bool whiteComp[3];
        bool whiteIsD50;
        // Possible bug in profiles: there are in fact some that says they contain that tag
        //    but in fact the pointer is null.
        //    Let's not crash on it anyway, and assume there is no white point instead.
        //    BUG:423685
        if (cmsIsTag(hsIMG, cmsSigMediaWhitePointTag)
            && (mediaWhitePointPtr = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigMediaWhitePointTag))) {

            mediaWhitePoint = *(mediaWhitePointPtr);
            baseMediaWhitePoint = mediaWhitePoint;

            whiteComp[0] = std::fabs(baseMediaWhitePoint.X - cmsD50_XYZ()->X) < 0.0001;
            whiteComp[1] = std::fabs(baseMediaWhitePoint.Y - cmsD50_XYZ()->Y) < 0.0001;
            whiteComp[2] = std::fabs(baseMediaWhitePoint.Z - cmsD50_XYZ()->Z) < 0.0001;
            whiteIsD50 = std::all_of(std::begin(whiteComp), std::end(whiteComp), [](bool b) {return b;});

            cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
            cmsCIEXYZ *CAM1;
            if (cmsIsTag(hsIMG, cmsSigChromaticAdaptationTag)
                && (CAM1 = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigChromaticAdaptationTag))
                && whiteIsD50) {
                //the chromatic adaption tag represent a matrix from the actual white point of the profile to D50.

                       //We first put all our data into structures we can manipulate.
                double d3dummy [3] = {mediaWhitePoint.X, mediaWhitePoint.Y, mediaWhitePoint.Z};
                QGenericMatrix<1, 3, double> whitePointMatrix(d3dummy);
                QTransform invertDummy(CAM1[0].X, CAM1[0].Y, CAM1[0].Z, CAM1[1].X, CAM1[1].Y, CAM1[1].Z, CAM1[2].X, CAM1[2].Y, CAM1[2].Z);
                //we then abuse QTransform's invert function because it probably does matrix inversion 20 times better than I can program.
                //if the matrix is uninvertable, invertedDummy will be an identity matrix, which for us means that it won't give any noticeable
                //effect when we start multiplying.
                QTransform invertedDummy = invertDummy.inverted();
                //we then put the QTransform into a generic 3x3 matrix.
                double d9dummy [9] = {invertedDummy.m11(), invertedDummy.m12(), invertedDummy.m13(),
                    invertedDummy.m21(), invertedDummy.m22(), invertedDummy.m23(),
                    invertedDummy.m31(), invertedDummy.m32(), invertedDummy.m33()
                };
                QGenericMatrix<3, 3, double> chromaticAdaptionMatrix(d9dummy);
                //multiplying our inverted adaption matrix with the whitepoint gives us the right whitepoint.
                QGenericMatrix<1, 3, double> result = chromaticAdaptionMatrix * whitePointMatrix;
                //and then we pour the matrix into the whitepoint variable. Generic matrix does row/column for indices even though it
                //uses column/row for initialising.
                mediaWhitePoint.X = result(0, 0);
                mediaWhitePoint.Y = result(1, 0);
                mediaWhitePoint.Z = result(2, 0);
                cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
            }
        }

        // in case missing whitepoint tag
        if (mediaWhitePoint.X == 0 && mediaWhitePoint.Y == 0 && mediaWhitePoint.Z == 0) {
            mediaWhitePoint = *cmsD50_XYZ();
            cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
        }

        d->p_wtptXYZ = mediaWhitePoint;

        cmsCIEXYZ *tempColorantsRed, *tempColorantsGreen, *tempColorantsBlue;
        // Note: don't assume that cmsIsTag is enough to check for errors; check the pointers, too
        // BUG:423685
        if (cmsIsTag(hsIMG, cmsSigRedColorantTag) && cmsIsTag(hsIMG, cmsSigRedColorantTag) && cmsIsTag(hsIMG, cmsSigRedColorantTag)
            && (tempColorantsRed = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigRedColorantTag))
            && (tempColorantsGreen = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigGreenColorantTag))
            && (tempColorantsBlue = (cmsCIEXYZ *)cmsReadTag(hsIMG, cmsSigBlueColorantTag))) {
            cmsCIEXYZTRIPLE tempColorants;
            tempColorants.Red = *tempColorantsRed;
            tempColorants.Green = *tempColorantsGreen;
            tempColorants.Blue = *tempColorantsBlue;

            cmsCIEXYZTRIPLE colorants;

            //convert to d65, this is useless.
            cmsAdaptToIlluminant(&colorants.Red, cmsD50_XYZ(), &mediaWhitePoint, &tempColorants.Red);
            cmsAdaptToIlluminant(&colorants.Green, cmsD50_XYZ(), &mediaWhitePoint, &tempColorants.Green);
            cmsAdaptToIlluminant(&colorants.Blue, cmsD50_XYZ(), &mediaWhitePoint, &tempColorants.Blue);

            d->colorants = colorants;
            d->hasColorants = true;
        } else {
            d->hasColorants = false;
        }
    }

    const cmsHTRANSFORM srgbtoxyz =
        cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
    const cmsHTRANSFORM imgtoxyz = [&]() {
        if (hsIMG) {
            return cmsCreateTransform(hsIMG, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
        } else {
            return cmsCreateTransform(hsRGB, TYPE_RGB_DBL, hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
        }
    }();

    const cmsHTRANSFORM xyztosrgb = cmsCreateTransform(hsXYZ, TYPE_XYZ_DBL, hsRGB, TYPE_RGB_FLT, displayPreviewIntent, 0);

    d->m_outCp = outCp;

    const int longestSize = std::max(imgSize.width(), imgSize.height());
    const int resizedLongSizeRatio = std::ceil((longestSize * 1.0) / (size * 1.0));
    const int resizedMaxPixelCount =
        (imgSize.width() / resizedLongSizeRatio) * (imgSize.height() / resizedLongSizeRatio);

    switch (depthId) {
    case Float32BitsColorDepthID: {
        // TODO: check if alpha exist here
        const quint8 chNum = 3;

        d->chR = 0;
        d->chG = 1;
        d->chB = 2;

        const int imgPixelCount = (rawData.size() / sizeof(float)) / chNum;
        const int resizeFactor = std::ceil((imgPixelCount * 1.0) / (resizedMaxPixelCount * 1.0));
        const int pixelSize = chNum * resizeFactor;

        QVector<const quint8 *> imgPointers;

        const auto *dataRaw = reinterpret_cast<const float *>(d->m_rawImageByte);
        for (int i = 0; i < rawData.size() / sizeof(float); i += pixelSize) {
            imgPointers.append(reinterpret_cast<const quint8 *>(dataRaw + i));
        }

        calculateFromRaw<float>(imgPointers, srgbtoxyz, imgtoxyz, xyztosrgb);
    } break;

    default:
        qDebug() << "depth id not implemented yet";
        break;
    }

    cmsDeleteTransform(srgbtoxyz);
    cmsDeleteTransform(imgtoxyz);
    cmsDeleteTransform(xyztosrgb);
}

template<typename T>
void ImageParserSC::calculateFromRaw(QVector<const quint8 *> &dataPointers,
                                     const cmsHTRANSFORM &srgbtoxyz,
                                     const cmsHTRANSFORM &imgtoxyz,
                                     const cmsHTRANSFORM &xyztosrgb)
{
    /*
     * Here still lies the main memory hog.. Apparently I can't append to a QVector in QtConcurrent
     * without calling mutex (which is very slow), so temporary storage is still needed before
     * transferring to the main array. Which can be massive on big images!
     */
    std::function<ColorPoint(const quint8 *)> convertXYYQC = [&](const quint8 *input) {
        const auto *dataRaw = reinterpret_cast<const T *>(input);
        const double pix[3] = {value<T>(dataRaw[d->chR]),
                               value<T>(dataRaw[d->chG]),
                               value<T>(dataRaw[d->chB])};

        cmsCIEXYZ bufXYZ;
        cmsCIEXYZ bufXYZadapt;
        cmsCIExyY bufxyY;
        float pixOut[3];

        if (pix[0] == 0.0 && pix[1] == 0.0 && pix[2] == 0.0) {
            bufxyY.x = d->m_prfWtpt.x();
            bufxyY.y = d->m_prfWtpt.y();
            bufxyY.Y = 0.0;
            pixOut[0] = 0.0;
            pixOut[1] = 0.0;
            pixOut[2] = 0.0;
        } else {
            cmsDoTransform(imgtoxyz, &pix, &bufXYZ, 1);
            cmsXYZ2xyY(&bufxyY, &bufXYZ);
            cmsAdaptToIlluminant(&bufXYZadapt, &d->p_wtptXYZ, cmsD50_XYZ(), &bufXYZ);
            cmsDoTransform(xyztosrgb, &bufXYZadapt, &pixOut, 1);
        }

        const ImageRGBFloat outRGB{pixOut[0], pixOut[1], pixOut[2]};

        const ImageXYZDouble output{bufxyY.x, bufxyY.y, bufxyY.Y};

        return ColorPoint(output, outRGB);
    };

    const cmsCIExyY profileWtpt = [&]() {
        const double white[3] = {1.0, 1.0, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(imgtoxyz, &white, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);
        if (d->p_wtptXYZ.X == 0 && d->p_wtptXYZ.Y == 0 && d->p_wtptXYZ.Z == 0)
            d->p_wtptXYZ = bufXYZ;
        return bufxyY;
    }();
    d->m_prfWtpt = QVector3D(profileWtpt.x, profileWtpt.y, profileWtpt.Y);

    {
        QFutureWatcher<ColorPoint> futureTmp;

        QProgressDialog pDial;
        pDial.setModal(true);

        pDial.setMinimum(0);
        pDial.setMaximum(dataPointers.size());
        pDial.setLabelText("Converting image to data points...");
        pDial.setCancelButtonText("Stop");

        QObject::connect(&futureTmp, &QFutureWatcher<void>::finished, &pDial, &QProgressDialog::reset);
        QObject::connect(&pDial, &QProgressDialog::canceled, &futureTmp, &QFutureWatcher<void>::cancel);
        QObject::connect(&futureTmp, &QFutureWatcher<void>::progressRangeChanged, &pDial, &QProgressDialog::setRange);
        QObject::connect(&futureTmp, &QFutureWatcher<void>::progressValueChanged, &pDial, &QProgressDialog::setValue);

        futureTmp.setFuture(QtConcurrent::mapped(dataPointers, convertXYYQC));

        pDial.exec();
        futureTmp.waitForFinished();

        if (futureTmp.isCanceled()) {
            return;
        }

        // I totally don't have idea why this one freaks out on Qt 5.15 Release build on WSL2...
        //    for (auto it = futureTmp.future().constBegin(); it != futureTmp.future().constEnd(); it++) {
        //        d->m_outCp->append(*it);
        //    }

        pDial.reset();
        pDial.setMinimum(0);
        pDial.setMaximum(futureTmp.future().resultCount());
        pDial.setLabelText("Appending image data...");
        pDial.setCancelButtonText("Stop");

        pDial.show();
        QGuiApplication::processEvents();

        quint64 it = 0;

        for (int i = 0; i < futureTmp.future().resultCount(); i++) {
            d->m_outCp->append(futureTmp.resultAt(i));
            it++;
            if (it % 1000000 == 0) {
                if (pDial.wasCanceled()) break;
                pDial.setValue(it);
                QGuiApplication::processEvents();
            }
        }

        pDial.close();
    }

    if (d->hasColorants) {
        cmsCIExyY bufR;
        cmsCIExyY bufG;
        cmsCIExyY bufB;

        cmsXYZ2xyY(&bufR, &d->colorants.Red);
        cmsXYZ2xyY(&bufG, &d->colorants.Green);
        cmsXYZ2xyY(&bufB, &d->colorants.Blue);

        const int sampleNum = 100;
        for (int i = 0; i < 3; i++) {
            cmsCIExyY pointA;
            cmsCIExyY pointB;
            if (i == 0) {
                pointA = bufR;
                pointB = bufG;
                d->m_outerGamut.append(ImageXYZDouble{bufR.x, bufR.y, bufR.Y});
            } else if (i == 1) {
                pointA = bufG;
                pointB = bufB;
                d->m_outerGamut.append(ImageXYZDouble{bufG.x, bufG.y, bufG.Y});
            } else if (i == 2) {
                pointA = bufB;
                pointB = bufR;
                d->m_outerGamut.append(ImageXYZDouble{bufB.x, bufB.y, bufB.Y});
            }
            for (int j = 1; j < sampleNum; j++) {
                const float frac = (float)j / (float)sampleNum;
                const float fX = lerp(pointA.x, pointB.x, frac);
                const float fY = lerp(pointA.y, pointB.y, frac);
                const float fYY = lerp(pointA.Y, pointB.Y, frac);
                d->m_outerGamut.append(ImageXYZDouble{fX, fY, fYY});
            }
        }
    } else {
        const int sampleNum = 100;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < sampleNum; j++) {
                const double red = [&]() {
                    if (i == 0)
                        return static_cast<double>(sampleNum - j) / sampleNum;
                    if (i == 2)
                        return static_cast<double>(j) / sampleNum;
                    return 0.0;
                }();
                const double green = [&]() {
                    if (i == 1)
                        return static_cast<double>(sampleNum - j) / sampleNum;
                    if (i == 0)
                        return static_cast<double>(j) / sampleNum;
                    return 0.0;
                }();
                const double blue = [&]() {
                    if (i == 2)
                        return static_cast<double>(sampleNum - j) / sampleNum;
                    if (i == 1)
                        return static_cast<double>(j) / sampleNum;
                    return 0.0;
                }();
                const double rgb[3] = {red, green, blue};
                cmsCIEXYZ bufXYZ;
                cmsCIExyY bufxyY;
                cmsDoTransform(imgtoxyz, &rgb, &bufXYZ, 1);
                cmsXYZ2xyY(&bufxyY, &bufXYZ);

                const ImageXYZDouble output{bufxyY.x, bufxyY.y, bufxyY.Y};
                d->m_outerGamut.append(output);
            }
        }
    }

    d->m_isSrgb = [&]() {
        const QVector<QColor> gmt = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
        for (auto &clr : gmt) {
            cmsCIEXYZ bufXYZsRGB;
            cmsCIEXYZ bufXYZImg;
            const double pix[3] = {clr.redF(), clr.greenF(), clr.blueF()};
            cmsDoTransform(srgbtoxyz, &pix, &bufXYZsRGB, 1);
            cmsDoTransform(imgtoxyz, &pix, &bufXYZImg, 1);
            if ((std::fabs(bufXYZsRGB.X - bufXYZImg.X) > 0.001) || (std::fabs(bufXYZsRGB.Y - bufXYZImg.Y) > 0.001)
                || (std::fabs(bufXYZsRGB.Z - bufXYZImg.Z) > 0.001)) {
                return false;
            }
        }
        return true;
    }();
}

void ImageParserSC::trimImage(quint64 size)
{
    if (!d->m_outCp || d->alreadyTrimmed) return;

    qDebug() << "Size before:" << d->m_outCp->size();

    // First trim: place into unordered set array to clear out duplicate (same color) pixels
    std::unordered_set<ColorPoint> imm;

    QProgressDialog pDial;
    pDial.setModal(true);

    pDial.setMinimum(0);
    pDial.setMaximum(d->m_outCp->size());
    pDial.setLabelText("Finding duplicate colors...");
    pDial.setCancelButtonText("Stop");

    pDial.show();
    QGuiApplication::processEvents();

    quint64 it = 0;

    for (quint64 i = 0; i < d->m_outCp->size(); i++) {
        auto res = imm.insert(d->m_outCp->at(i));
        if (!res.second) {
            res.first->second.N = res.first->second.N + 1;
        }
        it++;
        if (it % 200000 == 0) {
            if (pDial.wasCanceled()) break;
            pDial.setValue(it);
            QGuiApplication::processEvents();
        }
    }

    pDial.close();

    d->m_outCp->clear();

    const auto iterSize = [&]() {
        if (size == 0) return (quint64)1;
        return std::max(imm.size() / size, (quint64)1);
    }();

    it = 0;
    quint64 itProg = 0;

    qDebug() << "Size unordered set:" << imm.size();

    quint64 maxOccurence = 0;
    float maxOccurenceLog = 0.0;
    const ColorPoint *maxcp{nullptr};

    pDial.reset();
    pDial.setMinimum(0);
    pDial.setMaximum(imm.size());
    pDial.setLabelText("Trimming...");
    pDial.setCancelButtonText("Stop");

    pDial.show();
    QGuiApplication::processEvents();

    // First trim part 2:
    // if unordered set is still big, iterate with set number of steps to resize
    for (const auto &cp: imm) {
        it++;
        if (it >= iterSize || size == 0) {
            // qDebug() << cp.first.X << cp.first.Y << cp.first.Z << cp.second.N << cp.second.R << cp.second.G << cp.second.B;
            if (std::isnan(cp.first.X) || std::isnan(cp.first.Y) || std::isnan(cp.first.Z)) {
                continue;
            }
            if (maxOccurence < cp.second.N) {
                maxcp = &cp;
                maxOccurence = cp.second.N;
            }
            it = 0;
            d->m_outCp->append(cp);
        }
        if (itProg % 1000000 == 0) {
            if (pDial.wasCanceled()) break;
            pDial.setValue(itProg);
            QGuiApplication::processEvents();
        }
        itProg++;
    }

    pDial.close();

    if (maxcp) {
        qDebug() << "Pixel with max occurence:{" << maxcp->second.R << maxcp->second.G << maxcp->second.B
                 << "} num:" << maxOccurence;
        maxOccurenceLog = std::log(static_cast<float>(maxOccurence));
    }

    qDebug() << "Size trim 1:" << d->m_outCp->size();

    // Second trim: randomly delete data until final set size is reached

    if (d->m_outCp->size() > size && size != 0) {
        pDial.reset();
        pDial.setMinimum(0);
        pDial.setMaximum(size);
        pDial.setLabelText("Trimming...(2nd pass)");
        pDial.setCancelButtonText("Stop");

        pDial.show();
        QGuiApplication::processEvents();

        it = 0;

        QRandomGenerator rng(QRandomGenerator::global()->generate());
        std::unordered_set<ColorPoint> imm2;

        while (true) {
            const quint64 indx = rng.bounded(d->m_outCp->size() - 1);

            const float alpha =
                std::min(std::max(std::log(static_cast<float>(d->m_outCp->at(indx).second.N)) / maxOccurenceLog, 0.01f),
                         1.0f);
            d->m_outCp->at(indx).second.N = alpha;

            const auto stat = imm2.insert(d->m_outCp->at(indx));

            if (stat.second) {
                it++;
                if (it % 1000 == 0) {
                    if (pDial.wasCanceled()) break;
                    pDial.setValue(it);
                    QGuiApplication::processEvents();
                }

                if (it == size) break;
            }
        }

        d->m_outCp->clear();
        d->m_outCp->squeeze();
        for (const auto &im: imm2) {
            d->m_outCp->append(im);
        }

        pDial.close();
    } else {
        for (quint32 i = 0; i < d->m_outCp->size(); i++) {
            const float alpha =
                std::min(std::max(std::log(static_cast<float>(d->m_outCp->at(i).second.N)) / maxOccurenceLog, 0.0001f),
                         1.0f);
            d->m_outCp->at(i).second.A = alpha;
        }
    }

    d->m_outCp->squeeze();

    qDebug() << "Size trim 2 (final):" << d->m_outCp->size();

    d->alreadyTrimmed = true;
}

QString ImageParserSC::getProfileName()
{
    return d->m_profileName;
}

QVector3D ImageParserSC::getWhitePointXYY()
{
    return QVector3D(d->m_prfWtpt.x(), d->m_prfWtpt.y(), d->m_prfWtpt.z());
}

QVector<ImageXYZDouble> *ImageParserSC::getXYYArray() const
{
    // deprecated
    return nullptr;
}

QVector<ImageXYZDouble> *ImageParserSC::getOuterGamut() const
{
    return &d->m_outerGamut;
}

QVector<QColor> *ImageParserSC::getQColorArray() const
{
    // deprecated
    return nullptr;
}

QByteArray *ImageParserSC::getRawICC() const
{
    if (d->m_rawProfile.size() > 0) {
        return &d->m_rawProfile;
    }
    return nullptr;
}

bool ImageParserSC::isMatchSrgb()
{
    return d->m_isSrgb;
}
