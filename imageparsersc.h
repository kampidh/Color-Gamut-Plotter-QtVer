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

class ImageParserSC
{
public:
    ImageParserSC(const QImage &imgIn, int size);
    ~ImageParserSC();

    QString getProfileName();
    QVector2D getWhitePointXY();
    QVector<QVector3D> getXYYArray();
    QVector<QVector3D> getOuterGamut();
    QVector<QColor> getQColorArray();

    bool isMatchSrgb();

private:
    class Private;
    Private* const d {nullptr};
};

#endif // IMAGEPARSERSC_H
