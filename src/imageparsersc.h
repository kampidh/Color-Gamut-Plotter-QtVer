/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#ifndef IMAGEPARSERSC_H
#define IMAGEPARSERSC_H

#include <QImage>
#include <QVector2D>
#include <QScopedPointer>
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
    QString getMaxOccurence();
    QVector3D getWhitePointXYY();
    QVector<ImageXYZDouble> *getXYYArray() const;
    QVector<ImageXYZDouble> *getOuterGamut() const;
    QVector<QColor> *getQColorArray() const;
    QByteArray *getRawICC() const;
    void trimImage(quint64 size = 0);

    bool isMatchSrgb();

private:
    template<typename T>
    void calculateFromRaw();

    void iccParseWPColorant();
    void iccPutTransforms();

    class Private;
    QScopedPointer<Private> d;
};

#endif // IMAGEPARSERSC_H
