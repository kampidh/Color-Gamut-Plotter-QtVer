/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#ifndef IMAGEPARSERSC_H
#define IMAGEPARSERSC_H

#include <QImage>
#include <QVector2D>
#include <lcms2.h>

#include "imageformats.h"

class ImageParserSC
{
public:
    ImageParserSC();
    ~ImageParserSC();

    void inputFile(const QImage &imgIn, int size);
    void inputFile(const QByteArray &rawData, const QByteArray &iccData, ImageColorDepthID depthId, QSize imgSize, int size);
    QString getProfileName();
    QVector2D getWhitePointXY();
    QVector<QVector3D> getXYYArray();
    QVector<QVector3D> getOuterGamut();
    QVector<QColor> getQColorArray();

    bool isMatchSrgb();

private:
    void calculateFromFloat(QVector<QVector3D> &imgData,
                            const cmsHTRANSFORM &srgbtoxyz,
                            const cmsHTRANSFORM &imgtoxyz,
                            const cmsHTRANSFORM &xyztosrgb);
    class Private;
    Private* const d {nullptr};
};

#endif // IMAGEPARSERSC_H
