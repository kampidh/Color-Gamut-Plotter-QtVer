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
#include <QRandomGenerator>

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
    double dst = (double(src) / double(std::numeric_limits<T>::max()));
    if (ClampNegative) dst = std::max(0.0, dst);
    if (ClampPositive) dst = std::min(1.0, dst);
    return dst;
}

const static float adaptationState = 0.0;
const static cmsUInt32Number displayPreviewIntent = INTENT_RELATIVE_COLORIMETRIC;
const static bool useMultithreadConversion = true;
const static bool skipTransparent = true;

class Q_DECL_HIDDEN ImageParserSC::Private
{
public:
    QSize m_dimension{};
    QString m_profileName{};
    QVector<ImageXYZDouble> m_outerGamut{};
    QVector3D m_prfWtpt{};

    QByteArray m_rawProfile;
    QByteArray m_rawBytes;

    QString m_maxOccStr{};

    quint8 chR{0};
    quint8 chG{1};
    quint8 chB{2};

    quint8 numChannels{4};

    const quint8 *m_rawImageByte;
    quint64 m_rawImageByteSize{0};
    quint64 m_maxOccurence{0};

    QVector<ColorPoint> *m_outCp{nullptr};

    bool m_isSrgb{false};
    bool hasColorants{false};
    bool alreadyTrimmed{false};

    cmsCIEXYZTRIPLE colorants;
    cmsCIEXYZ p_wtptXYZ{0,0,0};

    cmsHPROFILE hsIMG{nullptr};
    cmsHPROFILE hsRGB{nullptr};
    cmsHPROFILE hsScRGB{nullptr};
    cmsHPROFILE hsXYZ{nullptr};

    cmsHTRANSFORM srgbtoxyz{nullptr};
    cmsHTRANSFORM imgtoxyz{nullptr};
    cmsHTRANSFORM xyztosrgb{nullptr};
    cmsHTRANSFORM imgtosrgb{nullptr};
};

ImageParserSC::ImageParserSC()
    : d(new Private)
{
    // moved into inputFile()
}

ImageParserSC::~ImageParserSC()
{
    qDebug() << "parser deleted";

    if (d->srgbtoxyz) {
        cmsDeleteTransform(d->srgbtoxyz);
    }
    if (d->imgtoxyz) {
        cmsDeleteTransform(d->imgtoxyz);
    }
    if (d->xyztosrgb) {
        cmsDeleteTransform(d->xyztosrgb);
    }
    if (d->imgtosrgb) {
        cmsDeleteTransform(d->imgtosrgb);
    }

    if (d->hsIMG) {
        cmsCloseProfile(d->hsIMG);
    }
    if (d->hsRGB) {
        cmsCloseProfile(d->hsRGB);
    }
    if (d->hsScRGB) {
        cmsCloseProfile(d->hsScRGB);
    }
    if (d->hsXYZ) {
        cmsCloseProfile(d->hsXYZ);
    }

    delete d;
}

void ImageParserSC::inputFile(const QImage &imgIn, int size, QVector<ColorPoint> *outCp)
{
    const QByteArray imRawIcc = imgIn.colorSpace().iccProfile();
    const int imLongSide = std::max(imgIn.width(), imgIn.height());

    d->m_rawImageByte = imgIn.constBits();
    d->m_rawImageByteSize = imgIn.sizeInBytes();

    d->m_dimension = QSize(imgIn.size());

    // Do not adapt to illuminant on Absolute Colorimetric
    // cmsSetAdaptationState(adaptationState);

    d->hsIMG = cmsOpenProfileFromMem(imRawIcc.data(), imRawIcc.size());
    d->m_rawProfile.append(imRawIcc);
    iccPutTransforms();

    const quint8 chNum = 4;

    d->m_outCp = outCp;
    d->numChannels = 4;

    if (imgIn.pixelFormat().colorModel() == QPixelFormat::RGB) {
        d->chR = 0;
        d->chG = 1;
        d->chB = 2;
    } else if (imgIn.pixelFormat().colorModel() == QPixelFormat::BGR) {
        d->chR = 2;
        d->chG = 1;
        d->chB = 0;
    }

    QImage img = [&]() {
        if (imLongSide > size) {
            return imgIn.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        return QImage();
    }();

    const auto imFmt = imgIn.format();
    switch (imFmt) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied: {
        if (!img.isNull()) {
            d->m_rawImageByte = img.constBits();
            d->m_rawImageByteSize = img.sizeInBytes();
        }

        // 32 bit is always BGR?
        d->chR = 2;
        d->chG = 1;
        d->chB = 0;

        calculateFromRaw<quint8>();
    } break;

    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied: {
        if (!img.isNull()) {
            d->m_rawImageByte = img.constBits();
            d->m_rawImageByteSize = img.sizeInBytes();
        }

        calculateFromRaw<quint16>();
    } break;

    default: {
        QImage imm;

#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
        if (!img.isNull()) {
            img.convertTo(QImage::Format_RGBA64);
            d->m_rawImageByte = img.constBits();
            d->m_rawImageByteSize = img.sizeInBytes();
        } else {
            imm = imgIn.convertToFormat(QImage::Format_RGBA64);
            d->m_rawImageByte = imm.constBits();
            d->m_rawImageByteSize = imm.sizeInBytes();
        }

        calculateFromRaw<quint16>();
#else
        if (!img.isNull()) {
            if (!(img.format() == QImage::Format_RGBA32FPx4 || img.format() == QImage::Format_RGBA32FPx4_Premultiplied)) {
                img.convertTo(QImage::Format_RGBA32FPx4);
            }
            d->m_rawImageByte = img.constBits();
            d->m_rawImageByteSize = img.sizeInBytes();
        } else {
            if (!(img.format() == QImage::Format_RGBA32FPx4 || img.format() == QImage::Format_RGBA32FPx4_Premultiplied)) {
                imm = imgIn.convertedTo(QImage::Format_RGBA32FPx4);
                d->m_rawImageByte = imm.constBits();
                d->m_rawImageByteSize = imm.sizeInBytes();
            } else {
                d->m_rawImageByte = img.constBits();
                d->m_rawImageByteSize = img.sizeInBytes();
            }
        }

        calculateFromRaw<float>();
#endif
    } break;
    }
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

    d->hsIMG = cmsOpenProfileFromMem(iccData.data(), iccData.size());
    d->m_rawProfile.append(iccData);
    iccPutTransforms();

    d->m_outCp = outCp;
    d->numChannels = 3;

    const int longestSize = std::max(imgSize.width(), imgSize.height());
    const int resizedLongSizeRatio = std::ceil((longestSize * 1.0) / (size * 1.0));
    const int resizedMaxPixelCount =
        (imgSize.width() / resizedLongSizeRatio) * (imgSize.height() / resizedLongSizeRatio);

    const quint64 pixelCount = imgSize.width() * imgSize.height();

    switch (depthId) {
    case Float32BitsColorDepthID: {
        const quint8 chNum = rawData.size() / pixelCount / sizeof(float);
        d->numChannels = chNum;

        d->chR = 0;
        d->chG = 1;
        d->chB = 2;

        QByteArray res;
        if (pixelCount > resizedMaxPixelCount) {
            QRandomGenerator *rng(QRandomGenerator::global());
            quint64 it = 0;
            while (it < resizedMaxPixelCount) {
                const quint64 pixPointer = rng->bounded(static_cast<quint32>(pixelCount - 1)) * (sizeof(float) * chNum);
                res.append(rawData.mid(pixPointer, sizeof(float) * chNum));
                it++;
            }
            d->m_rawImageByte = reinterpret_cast<const quint8 *>(res.constData());
            d->m_rawImageByteSize = res.size();
        } else {
            d->m_rawImageByte = reinterpret_cast<const quint8 *>(rawData.constData());
            d->m_rawImageByteSize = rawData.size();
        }

        calculateFromRaw<float>();
    } break;

    default:
        qDebug() << "depth id not implemented yet";
        break;
    }
}

template<typename T>
void ImageParserSC::calculateFromRaw()
{
    d->m_isSrgb = [&]() {
        const QVector<QColor> gmt = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
        for (auto &clr : gmt) {
            cmsCIEXYZ bufXYZsRGB;
            cmsCIEXYZ bufXYZImg;
            const double pix[3] = {clr.redF(), clr.greenF(), clr.blueF()};
            cmsDoTransform(d->srgbtoxyz, &pix, &bufXYZsRGB, 1);
            cmsDoTransform(d->imgtoxyz, &pix, &bufXYZImg, 1);
            if ((std::fabs(bufXYZsRGB.X - bufXYZImg.X) > 0.001) || (std::fabs(bufXYZsRGB.Y - bufXYZImg.Y) > 0.001)
                || (std::fabs(bufXYZsRGB.Z - bufXYZImg.Z) > 0.001)) {
                return false;
            }
        }
        return true;
    }();

    const cmsCIExyY profileWtpt = [&]() {
        const double white[3] = {1.0, 1.0, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(d->imgtoxyz, &white, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);
        if (d->p_wtptXYZ.X == 0 && d->p_wtptXYZ.Y == 0 && d->p_wtptXYZ.Z == 0)
            d->p_wtptXYZ = bufXYZ;
        return bufxyY;
    }();
    d->m_prfWtpt = QVector3D(profileWtpt.x, profileWtpt.y, profileWtpt.Y);

    const bool needTransform = (d->imgtosrgb && !d->m_isSrgb);

    std::function<QPair<QByteArray, QByteArray>(const QByteArray)> convertChunk = [&](const QByteArray input) {
        QByteArray trimXyz;
        QByteArray trimRgb;
        const quint32 pxsize = input.size() / sizeof(double) / 3;
        trimXyz.resize(input.size());
        cmsDoTransform(d->imgtoxyz, input.constData(), trimXyz.data(), pxsize);
        if (needTransform) {
            trimRgb.resize(input.size() / 2);
            cmsDoTransform(d->imgtosrgb, input.constData(), trimRgb.data(), pxsize);
            // return QPair<QByteArray, QByteArray>{trimXyz, trimRgb};
        } else {
            for (quint32 i = 0; i < input.size(); i += sizeof(double) * 3) {
                const double *rgbin = reinterpret_cast<const double *>(input.constData() + i);
                const float rgb[3] = {static_cast<float>(rgbin[0]),
                                      static_cast<float>(rgbin[1]),
                                      static_cast<float>(rgbin[2])};
                trimRgb.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&rgb), sizeof(rgb)));
            }
        }
        return QPair<QByteArray, QByteArray>{trimXyz, trimRgb};
    };

    const T* rawImPtr = reinterpret_cast<const T*>(d->m_rawImageByte);

    QByteArray rawTrimData;
    QByteArray rawTrimXyz;
    QByteArray rawTrimRgb;
    quint32 trimmedSize = 0;
    {
        QProgressDialog pDial;
        pDial.setModal(true);

        QVector<quint32> numOcc;
        {
            pDial.reset();
            pDial.setMinimum(0);
            pDial.setMaximum(d->m_rawImageByteSize / sizeof(T));
            pDial.setLabelText("Finding duplicate colors...");
            pDial.setCancelButtonText("Stop");

            pDial.show();
            QGuiApplication::processEvents();

            quint64 it = 0;
            quint64 skipAlpha = 0;

            std::unordered_set<ImageRGBTyped<T>> irgbTrim;
            for (quint64 i = 0; i < d->m_rawImageByteSize / sizeof(T); i += d->numChannels) {
                const T r = rawImPtr[i+d->chR];
                const T g = rawImPtr[i+d->chG];
                const T b = rawImPtr[i+d->chB];
                const ImageRGBTyped<T> imgin{r,g,b};

                if (d->numChannels == 4 && skipTransparent) {
                    const T a = rawImPtr[i+3];
                    if (a == 0) {
                        skipAlpha++;
                        continue;
                    }
                }

                const auto res = irgbTrim.insert(imgin);
                if (!res.second) {
                    res.first->N = res.first->N + 1;
                } else {
                    const double r = value<T>(res.first->R);
                    const double g = value<T>(res.first->G);
                    const double b = value<T>(res.first->B);
                    rawTrimData.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&r), sizeof(r)));
                    rawTrimData.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&g), sizeof(g)));
                    rawTrimData.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&b), sizeof(b)));
                }

                if (i % 200000 == 0) {
                    if (pDial.wasCanceled()) break;
                    pDial.setValue(i);
                    QGuiApplication::processEvents();
                }
            }

            qDebug() << "Skipped pixels (transparent):" << skipAlpha;

            pDial.close();

            pDial.reset();
            pDial.setMinimum(0);
            pDial.setMaximum(irgbTrim.size());
            pDial.setLabelText("Transferring...");
            pDial.setCancelButtonText("Stop");

            pDial.show();
            QGuiApplication::processEvents();

            ImageRGBTyped<T> maxocc;
            for (const auto &i : irgbTrim) {
                if (maxocc.N < i.N) {
                    maxocc = i;
                }
                numOcc.append(i.N);
            }

            d->m_maxOccurence = maxocc.N;

            trimmedSize = irgbTrim.size();
            qDebug() << "From to" << d->m_rawImageByteSize / sizeof(T) / d->numChannels << irgbTrim.size();
            {
                if (std::numeric_limits<T>::is_integer) {
                    d->m_maxOccStr = QString("Pixel with max occurence: RGB[%1, %2, %3] : %4")
                                         .arg(QString::number(maxocc.R),
                                              QString::number(maxocc.G),
                                              QString::number(maxocc.B),
                                              QString::number(maxocc.N));
                } else {
                    d->m_maxOccStr = QString("Pixel with max occurence: RGB[%1, %2, %3] : %4")
                                         .arg(QString::number(maxocc.R, 'f', 4),
                                              QString::number(maxocc.G, 'f', 4),
                                              QString::number(maxocc.B, 'f', 4),
                                              QString::number(maxocc.N));
                }
            }
            qDebug() << d->m_maxOccStr;

            pDial.close();
        }

        if (!rawTrimData.isEmpty()) {
            if (!useMultithreadConversion) {
                // Single threaded
                pDial.reset();
                pDial.setMinimum(0);
                pDial.setMaximum(0);
                pDial.setLabelText("Converting SP...");
                pDial.setCancelButtonText("Stop");

                pDial.show();
                QGuiApplication::processEvents();

                rawTrimXyz.resize(rawTrimData.size());
                cmsDoTransform(d->imgtoxyz, rawTrimData.constData(), rawTrimXyz.data(), trimmedSize);
                if (needTransform) {
                    rawTrimRgb.resize(rawTrimData.size());
                    cmsDoTransform(d->imgtosrgb, rawTrimData.constData(), rawTrimRgb.data(), trimmedSize);
                } else {
                    for (quint32 i = 0; i < rawTrimData.size(); i += sizeof(double) * 3) {
                        const double *rgbin = reinterpret_cast<const double *>(rawTrimData.constData() + i);
                        const float rgb[3] = {static_cast<float>(rgbin[0]),
                                              static_cast<float>(rgbin[1]),
                                              static_cast<float>(rgbin[2])};
                        rawTrimRgb.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&rgb), sizeof(rgb)));
                    }
                }
            } else {
                // Multi threaded
                int pos = 0, arrsize = rawTrimData.size(), sizeInArray = 100000 * sizeof(double) * 3;
                QList<QByteArray> arrays;
                while (pos < arrsize) {
                    QByteArray arr = rawTrimData.mid(pos, sizeInArray);
                    arrays << arr;
                    pos += arr.size();
                }

                rawTrimData.clear();
                rawTrimData.squeeze();

                QFutureWatcher<QPair<QByteArray, QByteArray>> futureTmp;

                pDial.reset();
                pDial.setMinimum(0);
                pDial.setMaximum(arrays.size());
                pDial.setLabelText("Converting MP...");
                pDial.setCancelButtonText("Stop");

                QObject::connect(&futureTmp, &QFutureWatcher<void>::finished, &pDial, &QProgressDialog::reset);
                QObject::connect(&pDial, &QProgressDialog::canceled, &futureTmp, &QFutureWatcher<void>::cancel);
                QObject::connect(&futureTmp,
                                 &QFutureWatcher<void>::progressRangeChanged,
                                 &pDial,
                                 &QProgressDialog::setRange);
                QObject::connect(&futureTmp,
                                 &QFutureWatcher<void>::progressValueChanged,
                                 &pDial,
                                 &QProgressDialog::setValue);

                futureTmp.setFuture(QtConcurrent::mapped(arrays, convertChunk));

                pDial.exec();
                futureTmp.waitForFinished();

                for (int i = 0; i < futureTmp.future().resultCount(); i++) {
                    rawTrimXyz.append(futureTmp.resultAt(i).first);
                    rawTrimRgb.append(futureTmp.resultAt(i).second);
                }
            }

            pDial.close();

            if (rawTrimXyz.size() / 2 != rawTrimRgb.size()) {
                qWarning("Err: Different sized vector");
                return;
            }

            pDial.reset();
            pDial.setMinimum(0);
            pDial.setMaximum(0);
            pDial.setLabelText("Appending...");
            pDial.setCancelButtonText("Stop");

            pDial.show();
            QGuiApplication::processEvents();

            float maxOccurenceLog = 0.0;

            maxOccurenceLog = std::log(static_cast<float>(d->m_maxOccurence));

            // QByteArray rawTrimXyy;
            // QByteArray trimRgbWithAlpha;

            for (quint64 i = 0; i < trimmedSize; i++) {
                const quint64 bNdx = i * (sizeof(double) * 3);

                const double* iXyz = reinterpret_cast<const double*>(rawTrimXyz.constData() + bNdx);
                const float* iRgb = reinterpret_cast<const float*>(rawTrimRgb.constData() + (bNdx / 2));

                const cmsCIEXYZ pix{iXyz[0], iXyz[1], iXyz[2]};
                cmsCIExyY bufxyY;

                cmsXYZ2xyY(&bufxyY, &pix);

                const float alpha =
                    std::min(std::max(std::log(static_cast<float>(numOcc.at(i))) / maxOccurenceLog, 0.01f),
                             1.0f);

                const ImageRGBFloat irgba = [&]() {
                    return ImageRGBFloat{iRgb[0], iRgb[1], iRgb[2], numOcc.at(i), alpha};
                }();

                // temporary to output to file
                // double xyytraspose[3] = {bufxyY.x - profileWtpt.x, bufxyY.y - profileWtpt.y, bufxyY.Y};
                // rawTrimXyy.append(QByteArray::fromRawData(reinterpret_cast<const char *>(&xyytraspose), sizeof(xyytraspose)));

                // const float colrgba[4] = {iRgb[0], iRgb[1], iRgb[2], alpha};
                // trimRgbWithAlpha.append(QByteArray::fromRawData(reinterpret_cast<const char*>(&colrgba), sizeof(colrgba)));

                const ImageXYZDouble output = [&]() {
                    if (bufxyY.x != bufxyY.x || bufxyY.y != bufxyY.y || bufxyY.Y != bufxyY.Y) {
                        return ImageXYZDouble{d->m_prfWtpt.x(), d->m_prfWtpt.y(), 0.0};
                    }
                    return ImageXYZDouble{bufxyY.x, bufxyY.y, bufxyY.Y};
                }();

                const ColorPoint cp{output, irgba};

                d->m_outCp->append(cp);
            }

            // {
            //     const QString head = QString("DOUBLE VEC3 %1\n").arg(QString::number(rawTrimXyy.size() / sizeof(double) / 3));
            //     QFile out("./output.theplot");
            //     out.open(QIODevice::WriteOnly);
            //     // out.write(head.toUtf8());
            //     out.write(rawTrimXyy);
            //     out.write(QString("/SPLITDATA/").toUtf8());
            //     out.write(trimRgbWithAlpha);
            //     out.close();
            // }

            pDial.close();
        }
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
                cmsDoTransform(d->imgtoxyz, &rgb, &bufXYZ, 1);
                cmsXYZ2xyY(&bufxyY, &bufXYZ);

                const ImageXYZDouble output{bufxyY.x, bufxyY.y, bufxyY.Y};
                d->m_outerGamut.append(output);
            }
        }
    }
}

void ImageParserSC::trimImage(quint64 size)
{
    if (!d->m_outCp || d->alreadyTrimmed) return;

    qDebug() << "Size before:" << d->m_outCp->size();

    QProgressDialog pDial;
    pDial.setModal(true);

    quint64 it = 0;

    quint64 maxOccurence = d->m_maxOccurence;
    float maxOccurenceLog = 0.0;

    maxOccurenceLog = std::log(static_cast<float>(maxOccurence));

    if (d->m_outCp->size() > size && size != 0) {
        pDial.reset();
        pDial.setMinimum(0);
        pDial.setMaximum(size);
        pDial.setLabelText("Trimming...(2nd pass)");
        pDial.setCancelButtonText("Stop");

        pDial.show();
        QGuiApplication::processEvents();

        it = 0;

        QRandomGenerator *rng(QRandomGenerator::global());
        std::unordered_set<ColorPoint> imm2;

        while (true) {
            const quint64 indx = rng->bounded(d->m_outCp->size() - 1);
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
    }

    d->m_outCp->squeeze();

    qDebug() << "Size trim 2 (final):" << d->m_outCp->size();

    d->alreadyTrimmed = true;
}

void ImageParserSC::iccParseWPColorant()
{
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
    if (cmsIsTag(d->hsIMG, cmsSigMediaWhitePointTag)
        && (mediaWhitePointPtr = (cmsCIEXYZ *)cmsReadTag(d->hsIMG, cmsSigMediaWhitePointTag))) {

        mediaWhitePoint = *(mediaWhitePointPtr);
        baseMediaWhitePoint = mediaWhitePoint;

        whiteComp[0] = std::fabs(baseMediaWhitePoint.X - cmsD50_XYZ()->X) < 0.0001;
        whiteComp[1] = std::fabs(baseMediaWhitePoint.Y - cmsD50_XYZ()->Y) < 0.0001;
        whiteComp[2] = std::fabs(baseMediaWhitePoint.Z - cmsD50_XYZ()->Z) < 0.0001;
        whiteIsD50 = std::all_of(std::begin(whiteComp), std::end(whiteComp), [](bool b) {return b;});

        cmsXYZ2xyY(&whitePoint, &mediaWhitePoint);
        cmsCIEXYZ *CAM1;
        if (cmsIsTag(d->hsIMG, cmsSigChromaticAdaptationTag)
            && (CAM1 = (cmsCIEXYZ *)cmsReadTag(d->hsIMG, cmsSigChromaticAdaptationTag))
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
    if (cmsIsTag(d->hsIMG, cmsSigRedColorantTag) && cmsIsTag(d->hsIMG, cmsSigRedColorantTag) && cmsIsTag(d->hsIMG, cmsSigRedColorantTag)
        && (tempColorantsRed = (cmsCIEXYZ *)cmsReadTag(d->hsIMG, cmsSigRedColorantTag))
        && (tempColorantsGreen = (cmsCIEXYZ *)cmsReadTag(d->hsIMG, cmsSigGreenColorantTag))
        && (tempColorantsBlue = (cmsCIEXYZ *)cmsReadTag(d->hsIMG, cmsSigBlueColorantTag))) {
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

void ImageParserSC::iccPutTransforms()
{
    // Do not adapt to illuminant on Absolute Colorimetric
    cmsSetAdaptationState(adaptationState);

    d->hsRGB = cmsCreate_sRGBProfile();

    /*
     * Reserved in case I need linear / scRGB
     * maybe Qt 6+ where there will be Float QImage?
     */
    cmsToneCurve *linTRC = cmsBuildGamma(NULL, 1.0);
    cmsToneCurve *linrgb[3]{linTRC, linTRC, linTRC};
    const cmsCIExyY sRgbD65 = {0.3127, 0.3290, 1.0000};
    const cmsCIExyYTRIPLE srgbPrim = {{0.6400, 0.3300, 0.2126},
                                      {0.3000, 0.6000, 0.7152},
                                      {0.1500, 0.0600, 0.0722}};
    d->hsScRGB = cmsCreateRGBProfile(&sRgbD65, &srgbPrim, linrgb);

    d->hsXYZ = cmsCreateXYZProfile();

    if (d->hsIMG) {
        cmsUInt32Number tmpSize = 0;
        tmpSize = cmsGetProfileInfo(d->hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
        wchar_t *tmp = new wchar_t[tmpSize];
        cmsGetProfileInfo(d->hsIMG, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, tmp, tmpSize);
        d->m_profileName = QString::fromWCharArray(tmp);
        delete[] tmp;

        iccParseWPColorant();
    }

    d->srgbtoxyz =
        cmsCreateTransform(d->hsRGB, TYPE_RGB_DBL, d->hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
    d->imgtoxyz = [&]() {
        if (d->hsIMG) {
            return cmsCreateTransform(d->hsIMG, TYPE_RGB_DBL, d->hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
        } else {
            return cmsCreateTransform(d->hsRGB, TYPE_RGB_DBL, d->hsXYZ, TYPE_XYZ_DBL, INTENT_ABSOLUTE_COLORIMETRIC, 0);
        }
    }();

    if (d->hsIMG) {
        d->imgtosrgb = cmsCreateTransform(d->hsIMG, TYPE_RGB_DBL, d->hsRGB, TYPE_RGB_FLT, displayPreviewIntent, 0);
    }

    d->xyztosrgb = cmsCreateTransform(d->hsXYZ, TYPE_XYZ_DBL, d->hsRGB, TYPE_RGB_FLT, displayPreviewIntent, 0);
}

QString ImageParserSC::getProfileName()
{
    return d->m_profileName;
}

QString ImageParserSC::getMaxOccurence()
{
    return d->m_maxOccStr;
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
