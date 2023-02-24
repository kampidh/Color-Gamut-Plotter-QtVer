/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#ifndef SCATTERDIALOG_H
#define SCATTERDIALOG_H

#include "imageparsersc.h"
#include <QWidget>

class ScatterDialog : public QWidget
{
    Q_OBJECT
public:
    //    explicit ScatterDialog(QWidget *parent, const QImage &inImage, QString fName, int plotType, int plotDensity);
    explicit ScatterDialog(QWidget *parent, ImageParserSC &inImage, QString fName, int plotType, int plotDensity);
    ~ScatterDialog();

    void saveButtonPress();

private:
    class Private;
    Private *const d{nullptr};
};

#endif // SCATTERDIALOG_H
