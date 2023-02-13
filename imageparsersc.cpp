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

#include <algorithm>
#include <cmath>

#include <lcms2.h>

class Q_DECL_HIDDEN ImageParserSC::Private
{
public:
    QSize m_dimension{};
    QString m_profileName{};
    QVector<cmsCIExyY> m_outxyY{};
    QVector<cmsCIExyY> m_outerGamut{};
    QVector<QColor> m_outQC{};
    QVector3D m_prfWtpt{};

    bool m_isSrgb{false};
};

ImageParserSC::ImageParserSC(const QImage &imgIn, int size)
    : d(new Private)
{
    const QByteArray imRawIcc = imgIn.colorSpace().iccProfile();
    const int imLongSide = std::max(imgIn.width(), imgIn.height());
    const QImage img = [&]() {
        if (imLongSide > size) {
            return imgIn.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        return imgIn;
    }();

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

    const cmsHTRANSFORM xyztosrgb = [&]() {
        if (hsIMG) {
            return cmsCreateTransform(hsXYZ, TYPE_XYZ_DBL, hsRGB, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, 0);
        } else {
            return cmsCreateTransform(hsXYZ, TYPE_XYZ_DBL, hsRGB, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, 0);
        }
    }();

    const uint32_t imgSZ = img.width() * img.height();

    QProgressDialog *pDial = new QProgressDialog();
    pDial->setMinimum(0);
    pDial->setMaximum(imgSZ);
    pDial->setLabelText("Converting image to data points...");
    pDial->setCancelButtonText("Stop");

    pDial->show();

    uint32_t progress = 0;
    uint32_t pDivider = imgSZ / 50;

    for (int i = 0; i < img.width(); i++) {
        for (int j = 0; j < img.height(); j++) {
            const double pix[3] = {img.pixelColor(i, j).redF(),
                                   img.pixelColor(i, j).greenF(),
                                   img.pixelColor(i, j).blueF()};
            cmsCIEXYZ bufXYZ;
            cmsCIExyY bufxyY;

            cmsDoTransform(imgtoxyz, &pix, &bufXYZ, 1);
            cmsXYZ2xyY(&bufxyY, &bufXYZ);
            d->m_outxyY.append(bufxyY);

            double pixOut[3];
            QColor bufout;

            cmsDoTransform(xyztosrgb, &bufXYZ, &pixOut, 1);
            bufout.setRedF(pixOut[0]);
            bufout.setGreenF(pixOut[1]);
            bufout.setBlueF(pixOut[2]);
            d->m_outQC.append(bufout);

            // Using pixelColor directly works faster, but it
            // doesn't always returned an accurate colors, especially
            // on wider gamut profiles.
            //
            // d->m_outQC.append(img.pixelColor(i, j));

            progress++;
            if (progress % pDivider == 0) {
                pDial->setValue(progress);
                if (pDial->wasCanceled()) {
                    break;
                }
                QApplication::processEvents();
            }
        }
    }
    pDial->setValue(progress);

    const cmsCIExyY profileWtpt = [&]() {
        const double white[3] = {1.0, 1.0, 1.0};
        cmsCIEXYZ bufXYZ;
        cmsCIExyY bufxyY;
        cmsDoTransform(imgtoxyz, &white, &bufXYZ, 1);
        cmsXYZ2xyY(&bufxyY, &bufXYZ);
        return bufxyY;
    }();
    d->m_prfWtpt = QVector3D(profileWtpt.x, profileWtpt.y, profileWtpt.Y);

    for (int i = 0; i < 256; i++) {
        const QVector<QColor> gmt = {QColor(i, 255 - i, 0), QColor(0, i, 255 - i), QColor(255 - i, 0, i)};
        for (auto &clr : gmt) {
            cmsCIEXYZ bufXYZ;
            cmsCIExyY bufxyY;
            const double pix[3] = {clr.redF(), clr.greenF(), clr.blueF()};
            cmsDoTransform(imgtoxyz, &pix, &bufXYZ, 1);
            cmsXYZ2xyY(&bufxyY, &bufXYZ);
            d->m_outerGamut.append(bufxyY);
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

    cmsDeleteTransform(srgbtoxyz);
    cmsDeleteTransform(imgtoxyz);
    cmsDeleteTransform(xyztosrgb);
}

ImageParserSC::~ImageParserSC()
{
    delete d;
}

QString ImageParserSC::getProfileName()
{
    return d->m_profileName;
}

QVector2D ImageParserSC::getWhitePointXY()
{
    return QVector2D(d->m_prfWtpt.x(), d->m_prfWtpt.y());
}

QVector<QVector3D> ImageParserSC::getXYYArray()
{
    QVector<QVector3D> tmp{};
    for (auto &xyy : d->m_outxyY){
        tmp.append(QVector3D(xyy.x, xyy.y, xyy.Y));
    }
    return tmp;
}

QVector<QVector3D> ImageParserSC::getOuterGamut()
{
    QVector<QVector3D> tmp{};
    for (auto &xyy : d->m_outerGamut){
        tmp.append(QVector3D(xyy.x, xyy.y, xyy.Y));
    }
    return tmp;
}

QVector<QColor> ImageParserSC::getQColorArray()
{
    return d->m_outQC;
}

bool ImageParserSC::isMatchSrgb()
{
    return d->m_isSrgb;
}
