/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "scatterdialog.h"
#include "imageparsersc.h"
#include "scatter2dchart.h"
#include "custom3dchart.h"

#include "./gamutplotterconfig.h"

#ifdef HAVE_JPEGXL
#include "jxlreader.h"
#include "jxlwriter.h"
#endif

#include <QApplication>
#include <QDebug>

#include <QCheckBox>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QIODevice>
#include <QLabel>
#include <QMessageBox>
#include <QPaintDevice>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <QKeyEvent>
#include <QCloseEvent>

class Q_DECL_HIDDEN ScatterDialog::Private
{
public:
    QImage m_inImage;
    QString m_fName;
    QString m_profileName;
    QVector3D m_wtpt;
    int m_plotType{0};
    int m_plotDensity{0};
    QWidget *m_container;
    Scatter2dChart *m_2dScatter{nullptr};
    Custom3dChart *m_custom3d{nullptr};
    bool m_is2d{false};
    bool m_isFullscreen{false};
    bool m_overrideSettings{false};
    QSize m_lastSize;
    PlotSetting2D m_plotSetting;

    QVector<ColorPoint> inputImg;
};

ScatterDialog::ScatterDialog(QString fName, int plotType, int plotDensity, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(QStringLiteral("Plotting result"));

    if (plotType == 0) {
        d->m_is2d = true;
    }
    d->m_fName = fName;
    d->m_plotType = plotType;
    d->m_plotDensity = plotDensity;

    setAttribute(Qt::WA_DeleteOnClose);

    qApp->installEventFilter(this);
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
    imgDetailLbl->setToolTip(QString());
    QGuiApplication::processEvents();

    QElapsedTimer ti;
    ti.start();

    const auto st = ti.elapsed();
    QString maxOccString;

    {
        ImageParserSC parsedImgInternal;

#ifdef HAVE_JPEGXL
        QFileInfo fi(d->m_fName);
        if (fi.suffix() == "jxl" || d->m_fName.isEmpty()) {
            QProgressDialog pDial;
            pDial.setRange(0, 0);
            pDial.setLabelText("Opening image...");
            pDial.setModal(true);

            pDial.show();
            QGuiApplication::processEvents();

            JxlReader jxlfile(d->m_fName);
            QMessageBox msg;
            if (!jxlfile.processJxl()) {
                QMessageBox msg;
                msg.warning(this, "Warning", "Failed to open JXL file!");
                pDial.close();
                return false;
            }
            pDial.close();
            parsedImgInternal.inputFile(jxlfile.getRawImage(),
                                        jxlfile.getRawICC(),
                                        jxlfile.getImageColorDepth(),
                                        jxlfile.getImageDimension(),
                                        d->m_plotDensity,
                                        &d->inputImg);

            if (d->m_fName.isEmpty()) {
                d->m_fName = QString(
                    "Internally stored JXL art - <a "
                    "href='https://jxl-art.surma.technology/"
                    "?zcode="
                    "C89MKclQMDez4PJIzUzPKAEzg5xDFAwNuPyLMlPzShJLMvPzFAy5nDJLUlILgIqBMqEFxYm5BTmpCkZcwYWlqalVqVxcmWkKyQ"
                    "p2QIUKCroK4Qq6IAZQLFzbT9cvHChhAOSDpPwUdC3gbFegaQZcAA'>[source in jxl-art.surma.technology]</a>");
                imgDetailLbl->setToolTip(
                    QString("Link will open: "
                            "https://jxl-art.surma.technology/"
                            "?zcode="
                            "C89MKclQMDez4PJIzUzPKAEzg5xDFAwNuPyLMlPzShJLMvPzFAy5nDJLUlILgIqBMqEFxYm5BTmpCkZcwYWlqalVqV"
                            "xcmWkKyQp2QIUKCroK4Qq6IAZQLFzbT9cvHChhAOSDpPwUdC3gbFegaQZcAA"));
            }
        } else {
            QProgressDialog pDial;
            pDial.setRange(0, 0);
            pDial.setLabelText("Opening image...");
            pDial.setModal(true);

            pDial.show();
            QGuiApplication::processEvents();

#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
            QImageReader::setAllocationLimit(512);
#endif
            QImageReader reader(d->m_fName);
            const QImage imgs = reader.read();
            if (imgs.isNull()) {
                QMessageBox msg;
                msg.warning(this, "Warning", "Invalid or unsupported image format!");
                pDial.close();
                return false;
            }
            pDial.close();
            parsedImgInternal.inputFile(imgs, d->m_plotDensity, &d->inputImg);
        }
#else
        const QImage imgs(d->m_fName);
        if (imgs.isNull()) {
            QMessageBox msg;
            msg.warning(this, "Warning", "Invalid or unsupported image format!");
            return false;
        }
        parsedImgInternal.inputFile(imgs, d->m_plotDensity, &d->inputImg);
#endif

        if (d->inputImg.isEmpty()) {
            return false;
        }

        QVector<ImageXYZDouble> outGamut = *parsedImgInternal.getOuterGamut();
        d->m_profileName = parsedImgInternal.getProfileName();
        d->m_wtpt = parsedImgInternal.getWhitePointXYY();

        if (d->m_is2d) {
            parsedImgInternal.trimImage();
            d->m_2dScatter = new Scatter2dChart(layout()->widget());
            if (d->m_overrideSettings) {
                d->m_2dScatter->overrideSettings(d->m_plotSetting);
            }
            d->m_2dScatter->addDataPoints(d->inputImg, 2);
            d->m_2dScatter->addGamutOutline(outGamut, d->m_wtpt);
            if (QByteArray *cs = parsedImgInternal.getRawICC()) {
                d->m_2dScatter->addColorSpace(*cs);
            }
            layout()->replaceWidget(container, d->m_2dScatter);
            d->m_2dScatter->setFocus();
        }

        if (!d->m_is2d) {
            rstViewBtn->setVisible(false);
            if (d->m_plotDensity >= 10000) {
                parsedImgInternal.trimImage(0);
            } else if (d->m_plotDensity >= 4000) {
                parsedImgInternal.trimImage(4000000);
            } else {
                parsedImgInternal.trimImage(400000);
            }
            d->m_custom3d = new Custom3dChart(d->m_plotSetting, layout()->widget());
            d->m_custom3d->addDataPoints(d->inputImg, d->m_wtpt, outGamut);
            if (!d->m_custom3d->checkValidity()) {
                QMessageBox msgBox;
                msgBox.setText("Couldn't initialize the OpenGL context.");
                msgBox.exec();
                return false;
            }
            layout()->replaceWidget(container, d->m_custom3d);
            d->m_custom3d->setFocus();
        }
        maxOccString = parsedImgInternal.getMaxOccurence();
    }

    const auto ed = ti.elapsed();

    qDebug() << "encode time" << ed - st;

    QSize screenSize = screen()->size();

    if (!d->m_is2d) {
        rstViewBtn->setText(QStringLiteral("Top-down camera"));
    } else {
        rstViewBtn->setText(QStringLiteral("Reset view"));
    }

    const QString imLabel = [&]() {
        QString tmp = "<b>File name:</b> " + d->m_fName + "<br><b>Profile name:</b> ";
        if (!d->m_profileName.isEmpty()) {
            tmp += d->m_profileName;
        } else {
            tmp += "None (Assumed as sRGB)";
        }
        tmp += " | <b>Profile white:</b> ";
        const QVector3D wtpt = d->m_wtpt;
        tmp += "x: " + QString::number(wtpt.x()) + " | y: " + QString::number(wtpt.y());
        tmp += "<br><b>Color statistics:</b> " + maxOccString;
        return tmp;
    }();

    imgDetailLbl->setText(imLabel);

    connect(saveImageBtn, &QPushButton::clicked, this, &ScatterDialog::savePlotImage);
    connect(rstWindowBtn, &QPushButton::clicked, this, &ScatterDialog::resetWinDimension);

    if (d->m_is2d) {
        connect(rstViewBtn, &QPushButton::clicked, d->m_2dScatter, &Scatter2dChart::resetCamera);
    }

    layout()->setContentsMargins(9, 9, 9, 9);
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

    QStringList lfmts;

#ifdef HAVE_JPEGXL
    lfmts << QString("JPEG XL image (*.jxl)");
#endif
    lfmts << QString("PNG image (*.png)");
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    lfmts << QString("TIFF image (*.tif)");
#endif

    const QString fmts = lfmts.join(";;");

    const QString tmpFileName = QFileDialog::getSaveFileName(this, tr("Save plot as image"), infoDir, fmts);

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
        out = d->m_custom3d->takeTheShot();
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
            if (tmpFileName.endsWith(".jxl")) {
                switch (out.format()) {
                case QImage::Format_RGBA8888:
                case QImage::Format_RGBA8888_Premultiplied:
                case QImage::Format_ARGB32:
                case QImage::Format_ARGB32_Premultiplied:
                case QImage::Format_RGBA64:
                case QImage::Format_RGBA64_Premultiplied:
                    out.convertToColorSpace(QColorSpace::SRgb);
                    break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
                case QImage::Format_RGBA16FPx4:
                case QImage::Format_RGBA16FPx4_Premultiplied:
                case QImage::Format_RGBA32FPx4:
                case QImage::Format_RGBA32FPx4_Premultiplied:
                    out.convertToColorSpace(QColorSpace::SRgbLinear);
                    break;
#endif
                default:
                    out.convertToColorSpace(QColorSpace::SRgb);
                    break;
                }
            }
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

    QMessageBox pre;
    pre.setText("Saving image...");
    pre.setStandardButtons(QMessageBox::NoButton);
    pre.show();

    QGuiApplication::processEvents();
    QGuiApplication::processEvents();

    if (tmpFileName.endsWith(".jxl")) {
#ifdef HAVE_JPEGXL
        JxlWriter jxlw;
        if (jxlw.convert(&out, tmpFileName)) {
            pre.close();
            QGuiApplication::processEvents();
            QMessageBox msg;
            msg.setText("Image saved successfully");
            msg.exec();
        } else {
            pre.close();
            QMessageBox msg;
            msg.warning(this, "Error", "Image cannot be saved!");
        }
#endif
    } else {
        if (out.save(tmpFileName)) {
            pre.close();
            QGuiApplication::processEvents();
            QMessageBox msg;
            msg.setText("Image saved successfully");
            msg.exec();
        } else {
            pre.close();
            QGuiApplication::processEvents();
            QMessageBox msg;
            msg.warning(this, "Error", "Image cannot be saved!");
        }
    }
}

void ScatterDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11) {
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
// apparently it works here..
#if defined(Q_OS_WIN)
            if (windowHandle()) {
                HWND handle = reinterpret_cast<HWND>(windowHandle()->winId());
                SetWindowLongPtr(handle, GWL_STYLE, GetWindowLongPtr(handle, GWL_STYLE) | WS_BORDER);
            }
#endif
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

bool ScatterDialog::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        // qDebug() << "key " << keyEvent->key() << "from" << obj;
        if (obj->inherits("Scatter3dChart") || obj->inherits("Custom3dChart")) {
            keyPressEvent(keyEvent);
        }
    }
    return QObject::eventFilter(obj, event);
}

bool ScatterDialog::event(QEvent *event) {
// does this even works...? https://doc.qt.io/qt-6.5/windows-issues.html
// #if defined(Q_OS_WIN)
//     if (event->type() == QEvent::WinIdChange) {
//         if (windowHandle()) {
//             HWND handle = reinterpret_cast<HWND>(windowHandle()->winId());
//             SetWindowLongPtr(handle, GWL_STYLE, GetWindowLongPtr(handle, GWL_STYLE) | WS_BORDER);
//         }
//     }
// #endif
    return QWidget::event(event);
}
