/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "qevent.h"
#include "scatterdialog.h"

//#include <QEvent>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi(this);
    setAcceptDrops(true);

    connect(plotBtn, &QPushButton::clicked, this, &MainWindow::goPlot);
    connect(fnameOpenBtn, &QPushButton::clicked, this, &MainWindow::openFileName);
}

MainWindow::~MainWindow()
{
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->accept();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->accept();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    event->accept();
    fnameFieldTxt->setText(event->mimeData()->urls()[0].toLocalFile());
}

void MainWindow::openFileName()
{
    const QString tmpFileName = QFileDialog::getOpenFileName(this,
                                                             tr("Open Image"),
                                                             QDir::currentPath(),
                                                             tr("Image Files (*.png *.jpg *.bmp);;All Files (*)"));
    if (!tmpFileName.isEmpty()) {
        fnameFieldTxt->setText(tmpFileName);
    }
}

void MainWindow::goPlot()
{
    //    plotBtn->setEnabled(false);
    const QString fileName = fnameFieldTxt->text();

    if (fileName == "") {
        QMessageBox msg;
        msg.information(this, "Information", "File name is empty!");
        plotBtn->setEnabled(true);
        return;
    }

    const QImage imgs(fileName);
    if (imgs.isNull()) {
        QMessageBox msg;
        msg.warning(this, "Warning", "Invalid or unsupported image format!");
        plotBtn->setEnabled(true);
        return;
    }

    const int plotTypeIndex = plotTypeCmb->currentIndex();
    const int plotDensity = [&]() {
        switch (plotTypeIndex) {
        // 3D Mono
        case 0:
            if (plotDensCmb->currentIndex() == 0) {
                return 200;
            } else {
                return 500;
            }
            break;
        // 3D Color
        case 1:
            if (plotDensCmb->currentIndex() == 0) {
                return 50;
            } else {
                return 100;
            }
            break;
        // 2D
        case 2:
        default:
            if (plotDensCmb->currentIndex() == 0) {
                return 200;
            } else {
                return 1500;
            }
            break;
        }
        return 10;
    }();

    ScatterDialog *sc = new ScatterDialog(0, imgs, fileName, plotTypeIndex, plotDensity);
    sc->show();

    const QPoint midpos(sc->frameSize().width() / 2, sc->frameSize().height() / 2);
    sc->move(QGuiApplication::screens().at(0)->geometry().center() - midpos);

    //    QObject::connect(sc, &ScatterDialog::destroyed, plotBtn, &QPushButton::setEnabled);
}
