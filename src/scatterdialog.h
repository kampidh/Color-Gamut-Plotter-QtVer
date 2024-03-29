/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#ifndef SCATTERDIALOG_H
#define SCATTERDIALOG_H

#include "./ui_scatterdialog.h"
#include "plot_typedefs.h"

#include <QWidget>
#include <QScopedPointer>

class ScatterDialog : public QWidget, public Ui::ScatterDialog
{
    Q_OBJECT
public:
    ScatterDialog(QString fName, int plotType, int plotDensity, QWidget *parent = nullptr);
    ~ScatterDialog();

    bool startParse();
    void overrideSettings(const PlotSetting2D &plot);

    void savePlotImage();
    void resetWinDimension();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool event(QEvent *event) override;

private:
    class Private;
    QScopedPointer<Private> d;
};

#endif // SCATTERDIALOG_H
