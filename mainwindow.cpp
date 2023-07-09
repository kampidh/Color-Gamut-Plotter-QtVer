/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include <gamutplotterconfig.h>

#include "./ui_mainwindow.h"
#include "imageparsersc.h"
#include "mainwindow.h"
#include "qevent.h"
#include "scatterdialog.h"

#ifdef HAVE_JPEGXL
#include "jxlreader.h"
#endif

//#include <QEvent>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>

#include <QFloat16>

class Q_DECL_HIDDEN MainWindow::Private
{
public:
    ScatterDialog *sc{nullptr};
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , d(new Private)
{
    setupUi(this);
    setAcceptDrops(true);

    connect(plotBtn, &QPushButton::clicked, this, &MainWindow::goPlot);
    connect(fnameOpenBtn, &QPushButton::clicked, this, &MainWindow::openFileName);
}

MainWindow::~MainWindow()
{
    delete d;
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
                                                             tr("Image Files (*.png *.jpg *.bmp *.jxl);;All Files (*)"));
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

    const int plotTypeIndex = plotTypeCmb->currentIndex();
    const int plotDensNdx = plotDensCmb->currentIndex();
    const int plotDensity = [&]() {
        switch (plotTypeIndex) {
        // 3D Mono
        case 0:
            switch (plotDensNdx) {
            case 0:
                return 200;
                break;
            case 1:
                return 500;
                break;
            case 2:
            default:
                return 1000;
                break;
            }
            break;
        // 3D Color
        case 1:
            switch (plotDensNdx) {
            case 0:
                return 50;
                break;
            case 1:
            default:
                return 100;
                break;
            }
            break;
        // 2D
        case 2:
        default:
            switch (plotDensNdx) {
            case 0:
                return 200;
                break;
            case 1:
                return 1500;
                break;
            case 2:
            default:
                return 5000;
                break;
            }
            break;
        }
        return 10;
    }();

    ImageParserSC parsedImage;

#ifdef HAVE_JPEGXL
    QFileInfo fi(fileName);
    if (fi.suffix() == "jxl") {
        JxlReader jxlfile(fileName);
        QMessageBox msg;
        if (!jxlfile.processJxl()) {
            QMessageBox msg;
            msg.warning(this, "Warning", "Failed to open JXL file!");
            plotBtn->setEnabled(true);
            return;
        }
        parsedImage.inputFile(jxlfile.getRawImage(),
                              jxlfile.getRawICC(),
                              jxlfile.getImageColorDepth(),
                              jxlfile.getImageDimension(),
                              plotDensity);
        plotBtn->setEnabled(true);
    } else {
        const QImage imgs(fileName);
        if (imgs.isNull()) {
            QMessageBox msg;
            msg.warning(this, "Warning", "Invalid or unsupported image format!");
            plotBtn->setEnabled(true);
            return;
        }
        parsedImage.inputFile(imgs, plotDensity);
    }
#else
    const QImage imgs(fileName);
    if (imgs.isNull()) {
        QMessageBox msg;
        msg.warning(this, "Warning", "Invalid or unsupported image format!");
        plotBtn->setEnabled(true);
        return;
    }
    parsedImage.inputFile(imgs, plotDensity);
#endif

    d->sc = new ScatterDialog(parsedImage, fileName, plotTypeIndex, plotDensity);
    d->sc->show();

    const QPoint midpos(d->sc->frameSize().width() / 2, d->sc->frameSize().height() / 2);
    d->sc->move(QGuiApplication::screens().at(0)->geometry().center() - midpos);

    //    QObject::connect(sc, &ScatterDialog::destroyed, plotBtn, &QPushButton::setEnabled);
}
