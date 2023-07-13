/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#ifndef SCATTERDIALOG_H
#define SCATTERDIALOG_H

#include "./ui_scatterdialog.h"
#include "imageparsersc.h"
#include <QWidget>

class ScatterDialog : public QWidget, public Ui::ScatterDialog
{
    Q_OBJECT
public:
    ScatterDialog(QString fName, int plotType, int plotDensity, QWidget *parent = nullptr);
    ~ScatterDialog();

    bool startParse();

    void savePlotImage();
    void resetWinDimension();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    class Private;
    Private *const d{nullptr};
};

#endif // SCATTERDIALOG_H
