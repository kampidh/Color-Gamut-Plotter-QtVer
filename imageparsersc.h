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
#include "plot_typedefs.h"

class ImageParserSC
{
public:
    ImageParserSC();
    ~ImageParserSC();

    void inputFile(const QImage &imgIn, int size, QVector<ColorPoint> *outCp);
    void inputFile(const QByteArray &rawData, const QByteArray &iccData, ImageColorDepthID depthId, QSize imgSize, int size, QVector<ColorPoint> *outCp);
    QString getProfileName();
    QVector3D getWhitePointXYY();
    QVector<ImageXYZDouble> *getXYYArray() const;
    QVector<ImageXYZDouble> *getOuterGamut() const;
    QVector<QColor> *getQColorArray() const;

    bool isMatchSrgb();

private:
    void calculateFromFloat(QVector<QVector3D> &imgData,
                            const cmsHTRANSFORM &srgbtoxyz,
                            const cmsHTRANSFORM &imgtoxyz,
                            const cmsHTRANSFORM &xyztosrgb);

    template<typename T>
    void calculateFromRaw(QVector<const quint8 *> &dataPointers,
                          const cmsHTRANSFORM &srgbtoxyz,
                          const cmsHTRANSFORM &imgtoxyz,
                          const cmsHTRANSFORM &xyztosrgb);

    class Private;
    Private* const d {nullptr};
};

#endif // IMAGEPARSERSC_H
