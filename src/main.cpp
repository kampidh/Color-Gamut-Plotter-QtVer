/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <lcms2_fast_float.h>

#include "constant_dataset.h"

QVector<QVector2D> Blackbody_Locus = []() {
    /*
     *  Planckian locus approximation
     *
     *  References:
     *  Kang, B., Moon, O., Hong, C., Lee, H., Cho, B., & Kim, Y. (2002). Design of advanced color: Temperature control
     * system for HDTV applications. Journal of the Korean Physical Society, 41(6), 865-871.
     */

    QVector<QVector2D> tempLocus;

    for (int i = 1700; i <= 25000; i += 50) {
        const double temp = static_cast<float>(i);
        double x;
        double y;

        if (i <= 4000) {
            x = (-0.2661239 * (1.0e+9 / std::pow(temp, 3.0))) - (0.2343589 * (1.0e+6 / std::pow(temp, 2.0))) + (0.8776956 * (1.0e+3 / temp)) + 0.179910;
        } else {
            x = (-3.0258469 * (1.0e+9 / std::pow(temp, 3.0))) + (2.1070379 * (1.0e+6 / std::pow(temp, 2.0))) + (0.2226347 * (1.0e+3 / temp)) + 0.240390;
        }

        if (i <= 2222) {
            y = (-1.1063814 * std::pow(x, 3.0)) - (1.34811020 * std::pow(x, 2.0)) + (2.18555832 * x) - 0.20219683;
        } else if (i <= 4000) {
            y = (-0.9549476 * std::pow(x, 3.0)) - (1.37418593 * std::pow(x, 2.0)) + (2.09137015 * x) - 0.16748867;
        } else {
            y = (3.0817580 * std::pow(x, 3.0)) - (5.87338670 * std::pow(x, 2.0)) + (3.75112997 * x) - 0.37001483;
        }

        tempLocus.append({static_cast<float>(x), static_cast<float>(y)});
    }
    return tempLocus;
}();

QVector<QVector2D> Daylight_Locus = []() {
    QVector<QVector2D> tempLocus;

    for (int i = 4000; i <= 10000; i += 50) {
        const double temp = static_cast<float>(i);
        double x;
        double y;

        if (i <= 7000) {
            x = 0.244063 + (0.09911 * (1.0e+3 / temp)) + (2.9678 * (1.0e+6 / std::pow(temp, 2.0))) - (4.6070 * (1.0e+9 / std::pow(temp, 3.0)));
        } else {
            x = 0.237040 + (0.24748 * (1.0e+3 / temp)) + (1.9018 * (1.0e+6 / std::pow(temp, 2.0))) - (2.0064 * (1.0e+9 / std::pow(temp, 3.0)));
        }

        y = (-3.000 * std::pow(x, 2.0)) + (2.870 * x) - 0.275;

        tempLocus.append({static_cast<float>(x), static_cast<float>(y)});
    }
    return tempLocus;
}();

int main(int argc, char *argv[])
{
    cmsPlugin(cmsFastFloatExtensions());
    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}
