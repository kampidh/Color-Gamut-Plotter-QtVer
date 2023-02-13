/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <lcms2_fast_float.h>

int main(int argc, char *argv[])
{
    cmsPlugin(cmsFastFloatExtensions());
    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}
