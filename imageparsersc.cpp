/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "imageparsersc.h"
#include <QVector3D>

#include <QDebug>

#include <QApplication>
#include <QColorSpace>
#include <QImage>
#include <QProgressDialog>

#include <QFuture>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

#include <lcms2.h>

template<typename T, typename std::enable_if_t<!std::numeric_limits<T>::is_integer, int> = 1>
inline double value(const T src)
{
    return double(src);
}

template<typename T, typename std::enable_if_t<std::numeric_limits<T>::is_integer, int> = 1>
inline double value(const T src)
{
    return (double(src) / double(std::numeric_limits<T>::max()));
}

class Q_DECL_HIDDEN ImageParserSC::Private
{
public:
    QSize m_dimension{};
    QString m_profileName{};
    QVector<ImageXYZDouble> m_outerGamut{};
    QVector3D m_prfWtpt{};

    quint8 chR{0};
    quint8 chG{1};
    quint8 chB{2};

    const quint8 *m_rawImageByte;

    QVector<ColorPoint> *m_outCp{nullptr};

    bool m_isSrgb{false};
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
    cmsSetAdaptationState(0.0);

    const cmsHPROFILE hsIMG = cmsOpenProfileFromMem(imRawIcc.data(), imRawIcc.size());
    const cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();
    const cmsHPROFILE hsXYZ = cmsCreateXYZProfile();

    if (hsIMG) {
        cmsUInt32Number tmpSize = 0;
        tmpSize = cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
        wchar_t *tmp = new wchar_t[tmpSize];
        cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, tmp, tmpSize);
        d->m_profileName = QString::fromWCharArray(tmp);
        delete[] tmp;
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

    const cmsHTRANSFORM xyztosrgb = cmsCreateTransform(hsXYZ, TYPE_XYZ_DBL, hsRGB, TYPE_RGB_FLT, INTENT_RELATIVE_COLORIMETRIC, 0);

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
        const QImage img = [&]() {
            if (imLongSide > size) {
                return imgIn.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation);
            }
            return imgIn;
        }();

        QVector<QVector3D> imgData;

        for (int i = 0; i < img.width(); i++) {
            for (int j = 0; j < img.height(); j++) {
                imgData.append({static_cast<float>(img.pixelColor(i, j).redF()),
                                static_cast<float>(img.pixelColor(i, j).greenF()),
                                static_cast<float>(img.pixelColor(i, j).blueF())});
            }
        }

        calculateFromFloat(imgData, srgbtoxyz, imgtoxyz, xyztosrgb);
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

    cmsSetAdaptationState(0.0);

    const cmsHPROFILE hsIMG = cmsOpenProfileFromMem(iccData.data(), iccData.size());
    const cmsHPROFILE hsRGB = cmsCreate_sRGBProfile();
    const cmsHPROFILE hsXYZ = cmsCreateXYZProfile();

    if (hsIMG) {
        cmsUInt32Number tmpSize = 0;
        tmpSize = cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
        wchar_t *tmp = new wchar_t[tmpSize];
        cmsGetProfileInfo(hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, tmp, tmpSize);
        d->m_profileName = QString::fromWCharArray(tmp);
        delete[] tmp;
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

    const cmsHTRANSFORM xyztosrgb = cmsCreateTransform(hsXYZ, TYPE_XYZ_DBL, hsRGB, TYPE_RGB_FLT, INTENT_RELATIVE_COLORIMETRIC, 0);

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

void ImageParserSC::calculateFromFloat(QVector<QVector3D> &imgData,
                                       const cmsHTRANSFORM &srgbtoxyz,
                                       const cmsHTRANSFORM &imgtoxyz,
                                       const cmsHTRANSFORM &xyztosrgb)
{
    /*
     * Here still lies the main memory hog.. Apparently I can't append to a QVector in QtConcurrent
     * without calling mutex (which is very slow), so temporary storage is still needed before
     * transferring to the main array. Which can be massive on big images!
     */
    std::function<ColorPoint(const QVector3D &)> convertXYYQC = [&](const QVector3D &input) {
        const double pix[3] = {input.x(), input.y(), input.z()};

        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;

        cmsDoTransform(imgtoxyz, &pix, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        float pixOut[3];

        cmsDoTransform(xyztosrgb, &bufXYZ, &pixOut, 1);

        const ImageRGBFloat outRGB{pixOut[0], pixOut[1], pixOut[2]};

        const ImageXYZDouble output{bufxyY.x, bufxyY.y, bufxyY.Y};

        return ColorPoint(output, outRGB);
    };

    QFutureWatcher<ColorPoint> futureTmp;

    QProgressDialog pDial;
    pDial.setMinimum(0);
    pDial.setMaximum(imgData.size());
    pDial.setLabelText("Converting image to data points...");
    pDial.setCancelButtonText("Stop");

    QObject::connect(&futureTmp, &QFutureWatcher<void>::finished, &pDial, &QProgressDialog::reset);
    QObject::connect(&pDial, &QProgressDialog::canceled, &futureTmp, &QFutureWatcher<void>::cancel);
    QObject::connect(&futureTmp, &QFutureWatcher<void>::progressRangeChanged, &pDial, &QProgressDialog::setRange);
    QObject::connect(&futureTmp, &QFutureWatcher<void>::progressValueChanged, &pDial, &QProgressDialog::setValue);

    futureTmp.setFuture(QtConcurrent::mapped(imgData, convertXYYQC));

    pDial.exec();
    futureTmp.waitForFinished();

    if (futureTmp.isCanceled()) {
        return;
    }

    for (auto it = futureTmp.future().constBegin(); it != futureTmp.future().constEnd(); it++) {
        d->m_outCp->append(*it);
    }

    const cmsCIExyY profileWtpt = [&]() {
        const double white[3] = {1.0, 1.0, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(imgtoxyz, &white, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);
        return bufxyY;
    }();
    d->m_prfWtpt = QVector3D(profileWtpt.x, profileWtpt.y, profileWtpt.Y);

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
        cmsCIExyY bufxyY;

        cmsDoTransform(imgtoxyz, &pix, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);

        float pixOut[3];

        cmsDoTransform(xyztosrgb, &bufXYZ, &pixOut, 1);

        const ImageRGBFloat outRGB{pixOut[0], pixOut[1], pixOut[2]};

        const ImageXYZDouble output{bufxyY.x, bufxyY.y, bufxyY.Y};

        return ColorPoint(output, outRGB);
    };

    QFutureWatcher<ColorPoint> futureTmp;

    QProgressDialog pDial;
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

    for (auto it = futureTmp.future().constBegin(); it != futureTmp.future().constEnd(); it++) {
        d->m_outCp->append(*it);
    }

    const cmsCIExyY profileWtpt = [&]() {
        const double white[3] = {1.0, 1.0, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(imgtoxyz, &white, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);
        return bufxyY;
    }();
    d->m_prfWtpt = QVector3D(profileWtpt.x, profileWtpt.y, profileWtpt.Y);

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

QString ImageParserSC::getProfileName()
{
    return d->m_profileName;
}

QVector2D ImageParserSC::getWhitePointXY()
{
    return QVector2D(d->m_prfWtpt.x(), d->m_prfWtpt.y());
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

bool ImageParserSC::isMatchSrgb()
{
    return d->m_isSrgb;
}
