/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include <gamutplotterconfig.h>

#include "./ui_mainwindow.h"
#include "mainwindow.h"
#include "qevent.h"
#include "scatterdialog.h"
#include "plot_typedefs.h"

//#include <QEvent>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QWindow>

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

    if (plotTypeCmb->currentIndex() == 2) {
        override2dChk->setVisible(true);
    } else {
        override2dChk->setVisible(false);
    }

    connect(plotBtn, &QPushButton::clicked, this, &MainWindow::goPlot);
    connect(fnameOpenBtn, &QPushButton::clicked, this, &MainWindow::openFileName);
    connect(plotTypeCmb, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::displayOverrideOpts);

    adjustSize();
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

void MainWindow::displayOverrideOpts(int ndx)
{
    if (ndx == 2) {
        override2dChk->setVisible(true);
    } else {
        override2dChk->setVisible(false);
    }

    resize({100,100});

    adjustSize();
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
    plotBtn->setEnabled(false);
    const QString fileName = fnameFieldTxt->text();

    if (fileName == "") {
        QMessageBox msg;
        msg.information(this, "Information", "File name is empty!");
        plotBtn->setEnabled(true);
        return;
    }

    /*
     * Explicitly disable WebP for now, as I am still don't know how to patch it into Qt 5.15 yet...
     *
     * Note: if you're compiling this yourself and have already patched CVE-2023-4863 into
     * qtimageformats, you can disable this check and continue reading the WebP images.
     */
    if (QFileInfo(fileName).completeSuffix().contains("webp", Qt::CaseInsensitive)) {
        QMessageBox msg;
        msg.warning(this, "Warning", "webp images are currently disabled!\nReason: unpatched CVE-2023-4863");
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
                return 1000;
                break;
            case 2:
            default:
                return 5000;
                break;
            }
            break;
        // 3D Color
        case 1:
            switch (plotDensNdx) {
            case 0:
                return 100;
                break;
            case 1:
                return 500;
                break;
            case 2:
            default:
                return 2000;
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

    d->sc = new ScatterDialog(fileName, plotTypeIndex, plotDensity);

    if (override2dChk->isChecked() && plotTypeIndex == 2) {
        const PlotSetting2D plotSet{
            enableAAChk->isChecked(),
            forceBucketChk->isChecked(),
            use16BitChk->isChecked(),
            showStatChk->isChecked(),
            showGridsChk->isChecked(),
            showsRGBChk->isChecked(),
            showImgGamutChk->isChecked(),
            showMacAdamECHk->isChecked(),
            showColorCheckChk->isChecked(),
            showBlBodyLocChk->isChecked(),
            particleAlphaSpn->value(),
            particleSizeSpn->value(),
            renderScaleSpn->value()
        };

        d->sc->overrideSettings(plotSet);
    }

    if (!d->sc->startParse()) {
        delete d->sc;
        plotBtn->setEnabled(true);
        return;
    }

    plotBtn->setEnabled(true);
    d->sc->show();

    const QScreen *currentWin = window()->windowHandle()->screen();
    const QSize screenSize = currentWin->size();

    d->sc->resize(QSize(screenSize.height() / 1.3, screenSize.height() / 1.25));
    const QPoint midpos(d->sc->frameSize().width() / 2, d->sc->frameSize().height() / 2);
    d->sc->move(currentWin->geometry().center() - midpos);
}
