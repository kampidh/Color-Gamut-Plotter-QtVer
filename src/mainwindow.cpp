/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "mainwindow.h"
#include "scatterdialog.h"
#include "plot_typedefs.h"
#include "global_variables.h"
#include "gamutplotterconfig.h"

#include "qevent.h"

#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QWindow>

#include <QFloat16>

#ifdef HAVE_JPEGXL
#include <jxl/version.h>
#endif

bool ClampNegative = false;
bool ClampPositive = false;

class Q_DECL_HIDDEN MainWindow::Private
{
public:
    std::vector<ScatterDialog *> scList;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , d(new Private)
{
    setupUi(this);
    setAcceptDrops(true);

    if (plotTypeCmb->currentIndex() == 0) {
        override2dChk->setVisible(true);
    } else {
        override2dChk->setVisible(false);
    }

    if (plotTypeCmb->currentIndex() == 1) {
        override3dChk->setVisible(true);
    } else {
        override3dChk->setVisible(false);
    }

    connect(plotBtn, &QPushButton::clicked, this, &MainWindow::goPlot);
    connect(fnameOpenBtn, &QPushButton::clicked, this, &MainWindow::openFileName);
    connect(plotTypeCmb, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::displayOverrideOpts);

    QString lTitle = windowTitle() + " - v" + QString(PROJECT_VERSION);
    setWindowTitle(lTitle);

    QString lFooter = QString("Build using Qt ") + QT_VERSION_STR +
#ifdef HAVE_JPEGXL
        QString(" | libjxl %1.%2.%3")
            .arg(QString::number(JPEGXL_MAJOR_VERSION),
                 QString::number(JPEGXL_MINOR_VERSION),
                 QString::number(JPEGXL_PATCH_VERSION))
        +
#endif
        "\n" + lblFooter->text();
    lblFooter->setText(lFooter);

    resize(minimumSizeHint());
    setMinimumSize(minimumSizeHint());
    setMaximumSize(minimumSizeHint());
}

MainWindow::~MainWindow()
{
    d.reset();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->accept();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "closing active plots:" << d->scList.size();
    for (auto &sc : d->scList) {
        sc->close();
    }
    event->accept();
}

void MainWindow::getDestroyedPlot(QObject *plt)
{
    d->scList.erase(std::remove_if(d->scList.begin(), d->scList.end(), [&](ScatterDialog *sc) {
        return sc == plt;
    }), d->scList.end());
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
    if (ndx == 0) {
        override2dChk->setVisible(true);
    } else {
        override2dChk->setVisible(false);
    }

    if (ndx == 1) {
        override3dChk->setVisible(true);
    } else {
        override3dChk->setVisible(false);
    }

    // it's a bit jarring when resizing...

    QGuiApplication::processEvents();
    resize(minimumSizeHint());
    setMinimumSize(minimumSizeHint());
    setMaximumSize(minimumSizeHint());

    // adjustSize();
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

    // if (fileName == "") {
    //     QMessageBox msg;
    //     msg.information(this, "Information", "File name is empty!");
    //     plotBtn->setEnabled(true);
    //     return;
    // }

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
        // 2D
        case 0:
        case 1:
            switch (plotDensNdx) {
            case 0:
                return 1000;
                break;
            case 1:
                return 4000;
                break;
            case 2:
            default:
                return 10000;
                break;
            }
            break;
        }
        return 10;
    }();

    ScatterDialog *scd = new ScatterDialog(fileName, plotTypeIndex, plotDensity);

    ClampNegative = chkClampNeg->isChecked();
    ClampPositive = chkClampPos->isChecked();

    if (override2dChk->isChecked() && plotTypeIndex == 0) {
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

        scd->overrideSettings(plotSet);
    }

    if (plotTypeIndex == 1) {
        PlotSetting2D plotSet;
        plotSet.multisample3d = multisampleSpn->value();

        scd->overrideSettings(plotSet);
    }

    if (!scd->startParse()) {
        scd->deleteLater();
        plotBtn->setEnabled(true);
        return;
    }

    plotBtn->setEnabled(true);
    scd->show();

    const QScreen *currentWin = window()->windowHandle()->screen();
    const QSize screenSize = currentWin->size();

    scd->resize(QSize(screenSize.height() / 1.3, screenSize.height() / 1.25));
    const QPoint midpos(scd->frameSize().width() / 2, scd->frameSize().height() / 2);
    scd->move(currentWin->geometry().center() - midpos);

    connect(scd, &ScatterDialog::destroyed, this, &MainWindow::getDestroyedPlot);

    d->scList.push_back(std::move(scd));
}
