/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "scatterdialog.h"
#include "imageparsersc.h"
#include "scatter2dchart.h"
#include "scatter3dchart.h"

#include "./gamutplotterconfig.h"

#ifdef HAVE_JPEGXL
#include "jxlreader.h"
#endif

#include <QApplication>
#include <QDebug>

#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
using namespace QtDataVisualization;
#endif

#include <QCheckBox>
#include <QColorSpace>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QMessageBox>
#include <QPaintDevice>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#include <QKeyEvent>
#include <QCloseEvent>

#include <cstring>

class Q_DECL_HIDDEN ScatterDialog::Private
{
public:
    QImage m_inImage;
    QString m_fName;
    QString m_profileName;
    QVector3D m_wtpt;
    int m_plotType{0};
    int m_plotDensity{0};
    // Q3DScatter *m_graph;
    QWidget *m_container;
    Scatter3dChart *m_3dScatter;
    Scatter2dChart *m_2dScatter;
    bool m_is2d{false};
    bool m_isFullscreen{false};
    bool m_overrideSettings{false};
    QSize m_lastSize;
    PlotSetting2D m_plotSetting;

    QVector<ColorPoint> inputImg;
    ImageParserSC parsedImg;
};

ScatterDialog::ScatterDialog(QString fName, int plotType, int plotDensity, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(QStringLiteral("Plotting result"));

    if (plotType == 2) {
        d->m_is2d = true;
    }
    d->m_fName = fName;
    d->m_plotType = plotType;
    d->m_plotDensity = plotDensity;

    setAttribute(Qt::WA_DeleteOnClose);
}

ScatterDialog::~ScatterDialog()
{
    // move delete to close event
    // delete d;
}

void ScatterDialog::closeEvent(QCloseEvent *event)
{
    if (d->m_is2d) {
        d->m_2dScatter->cancelRender();
    }
    event->accept();
    delete d;
}

void ScatterDialog::overrideSettings(const PlotSetting2D &plot)
{
    d->m_plotSetting = plot;
    d->m_overrideSettings = true;
}

bool ScatterDialog::startParse()
{
    QCoreApplication::processEvents();

#ifdef HAVE_JPEGXL
    QFileInfo fi(d->m_fName);
    if (fi.suffix() == "jxl") {
        JxlReader jxlfile(d->m_fName);
        QMessageBox msg;
        if (!jxlfile.processJxl()) {
            QMessageBox msg;
            msg.warning(this, "Warning", "Failed to open JXL file!");
            return false;
        }
        d->parsedImg.inputFile(jxlfile.getRawImage(),
                              jxlfile.getRawICC(),
                              jxlfile.getImageColorDepth(),
                              jxlfile.getImageDimension(),
                              d->m_plotDensity,
                              &d->inputImg);
    } else {
        const QImage imgs(d->m_fName);
        if (imgs.isNull()) {
            QMessageBox msg;
            msg.warning(this, "Warning", "Invalid or unsupported image format!");
            return false;
        }
        d->parsedImg.inputFile(imgs, d->m_plotDensity, &d->inputImg);
    }
#else
    const QImage imgs(d->m_fName);
    if (imgs.isNull()) {
        QMessageBox msg;
        msg.warning(this, "Warning", "Invalid or unsupported image format!");
        return false;
    }
    d->parsedImg.inputFile(imgs, d->m_plotDensity, &d->inputImg);
#endif

    if (d->inputImg.isEmpty()) {
        return false;
    }

    QVector<ImageXYZDouble> outGamut = *d->parsedImg.getOuterGamut();
    d->m_profileName = d->parsedImg.getProfileName();
    d->m_wtpt = d->parsedImg.getWhitePointXYY();

    if (d->m_is2d) {
        d->m_2dScatter = new Scatter2dChart();
        if (d->m_overrideSettings) {
            d->m_2dScatter->overrideSettings(d->m_plotSetting);
        }
        d->m_2dScatter->addDataPoints(d->inputImg, 2);
        d->m_2dScatter->addGamutOutline(outGamut, d->m_wtpt);
        if (QByteArray *cs = d->parsedImg.getRawICC()) {
            d->m_2dScatter->addColorSpace(*cs);
        }
        layout()->replaceWidget(container, d->m_2dScatter);
        orthogonalViewChk->setVisible(false);
    }

    if (!d->m_is2d) {
        const bool isSrgb = d->parsedImg.isMatchSrgb();
        d->m_3dScatter = new Scatter3dChart();
        d->m_3dScatter->addDataPoints(d->inputImg, outGamut, isSrgb, d->m_plotType);
        layout()->replaceWidget(container, QWidget::createWindowContainer(d->m_3dScatter));

        if (!d->m_3dScatter->hasContext()) {
            QMessageBox msgBox;
            msgBox.setText("Couldn't initialize the OpenGL context.");
            msgBox.exec();
            return false;
        }
    }

    QSize screenSize = screen()->size();

    if (!d->m_is2d) {
        rstViewBtn->setText(QStringLiteral("Top-down camera"));
    } else {
        rstViewBtn->setText(QStringLiteral("Reset view"));
    }

    orthogonalViewChk->setChecked(false);
    orthogonalViewChk->setText("Orthogonal projection");

    const QString imLabel = [&]() {
        QString tmp = "<b>File name:</b> " + d->m_fName + "<br><b>Profile name:</b> ";
        if (!d->m_profileName.isEmpty()) {
            tmp += d->m_profileName;
        } else {
            tmp += "None (Assumed as sRGB)";
        }
        tmp += "<br><b>Profile white:</b> ";
        const QVector3D wtpt = d->m_wtpt;
        tmp += "x: " + QString::number(wtpt.x()) + " | y: " + QString::number(wtpt.y());
        return tmp;
    }();

    imgDetailLbl->setText(imLabel);

    connect(saveImageBtn, &QPushButton::clicked, this, &ScatterDialog::savePlotImage);
    connect(rstWindowBtn, &QPushButton::clicked, this, &ScatterDialog::resetWinDimension);

    if (!d->m_is2d) {
        connect(rstViewBtn, &QPushButton::clicked, d->m_3dScatter, &Scatter3dChart::changePresetCamera);
        connect(orthogonalViewChk, &QCheckBox::clicked, d->m_3dScatter, &Scatter3dChart::setOrthogonal);
    } else {
        connect(rstViewBtn, &QPushButton::clicked, d->m_2dScatter, &Scatter2dChart::resetCamera);
    }

    resize(QSize(screenSize.height() / 1.3, screenSize.height() / 1.25));

    return true;
}

void ScatterDialog::resetWinDimension()
{
    QSize screenSize = screen()->size();
    container->setMinimumSize(QSize(screenSize.height() / 3.0, screenSize.height() / 3.0));
    resize(QSize(screenSize.height() / 1.3, screenSize.height() / 1.25));
}

void ScatterDialog::savePlotImage()
{
    QFileInfo info(d->m_fName);
    QString infoDir = info.absolutePath();

    const QString tmpFileName = QFileDialog::getSaveFileName(this,
                                                             tr("Save plot as image"),
                                                             infoDir,
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
                                                             tr("Portable Network Graphics (*.png)"));
#else
                                                             tr("Portable Network Graphics (*.png);;TIFF image (*.tif)"));
#endif
    if (tmpFileName.isEmpty()) {
        return;
    }

    const auto useLabel =
        QMessageBox::question(this, "Save label", "Do you want to include filename and profile label attached?");

    QString chTitle(info.fileName());
    chTitle += "\n";
    if (!d->m_profileName.isEmpty()) {
        chTitle += d->m_profileName;
    } else {
        chTitle += "None (Assumed as sRGB)";
    }

    QImage out;

    if (!d->m_is2d) {
        out = d->m_3dScatter->renderToImage(8);
    } else {
        if (d->m_2dScatter->getFullPixmap()) {
            out = *d->m_2dScatter->getFullPixmap();
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
            if (tmpFileName.endsWith(".tif")) {
                out.convertTo(QImage::Format_RGBA32FPx4);
                out.convertToColorSpace(QColorSpace::SRgbLinear);
            } else if (tmpFileName.endsWith(".png")) {
                out.convertTo(QImage::Format_RGBA64);
                out.convertToColorSpace(QColorSpace::SRgb);
            }
#endif
        }
    }

    Q_ASSERT(!out.isNull());

    if (useLabel == QMessageBox::Yes) {
        QPainter pn;
        if (!pn.begin(&out)) {
            return;
        }
        pn.setPen(QPen(Qt::white));
        pn.setFont(QFont("Courier", 10, QFont::Medium));
        pn.drawText(out.rect(), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, chTitle);
        pn.end();
    }

    if (out.save(tmpFileName)) {
        QMessageBox msg;
        msg.setText("Image saved successfully");
        msg.exec();
    } else {
        QMessageBox msg;
        msg.warning(this, "Error", "Image cannot be saved!");
    }
}

void ScatterDialog::keyPressEvent(QKeyEvent *event)
{
    // fullscreen only for 2D since I don't know how to exit 3D fullscreen yet uwu
    if ((event->key() == Qt::Key_F11) && d->m_is2d) {
        if (!d->m_isFullscreen) {
            d->m_lastSize = size();
            d->m_isFullscreen = true;
            guiGroup->setVisible(false);
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
            layout()->setMargin(0);
#else
            layout()->setContentsMargins(0, 0, 0, 0);
#endif
            setWindowState(Qt::WindowFullScreen);
        } else {
            d->m_isFullscreen = false;
            setWindowState(Qt::WindowNoState);
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
            layout()->setMargin(9);
#else
            layout()->setContentsMargins(9, 9, 9, 9);
#endif
            guiGroup->setVisible(true);
            resize(d->m_lastSize);
        }
    } else if (event->key() == Qt::Key_F12) {
        savePlotImage();
    }
}
