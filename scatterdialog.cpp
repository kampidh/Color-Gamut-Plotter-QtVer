/*
 * SPDX-FileCopyrightText: 2023 Rasyuqa Asyira H <qampidh@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 **/

#include "scatterdialog.h"
#include "imageparsersc.h"
#include "scatter2dchart.h"
#include "scatter3dchart.h"

#include <QApplication>
#include <QDebug>

// using namespace QtDataVisualization;

#include <QCheckBox>
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

class Q_DECL_HIDDEN ScatterDialog::Private
{
public:
    QImage m_inImage;
    QString m_fName;
    QString m_profileName;
    QVector2D m_wtpt;
    int m_plotType = 0;
    int m_plotDensity = 0;
    Q3DScatter *m_graph;
    QWidget *m_container;
    Scatter3dChart *m_3dScatter;
    Scatter2dChart *m_2dScatter;
    bool m_is2d{false};
};

ScatterDialog::ScatterDialog(QWidget *parent, const QImage &inImage, QString fName, int plotType, int plotDensity)
    : QWidget(parent)
    , d(new Private)
{
    if (plotType == 2) {
        d->m_is2d = true;
    }
    d->m_fName = fName;
    d->m_inImage = inImage;
    d->m_plotType = plotType;
    d->m_plotDensity = plotDensity;

    ImageParserSC parsedImg(inImage, d->m_plotDensity);
    QVector<QVector3D> outxyY = parsedImg.getXYYArray();
    QVector<QVector3D> outGamut = parsedImg.getOuterGamut();
    QVector<QColor> outQC = parsedImg.getQColorArray();
    const bool isSrgb = parsedImg.isMatchSrgb();
    d->m_profileName = parsedImg.getProfileName();
    d->m_wtpt = parsedImg.getWhitePointXY();

    if (!d->m_is2d) {
        d->m_3dScatter = new Scatter3dChart();
        d->m_3dScatter->addDataPoints(outxyY, outQC, outGamut, isSrgb, d->m_plotType);

        d->m_container = QWidget::createWindowContainer(d->m_3dScatter);

        if (!d->m_3dScatter->hasContext()) {
            QMessageBox msgBox;
            msgBox.setText("Couldn't initialize the OpenGL context.");
            msgBox.exec();
            return;
        }
    }

    if (d->m_is2d) {
        d->m_2dScatter = new Scatter2dChart();
        d->m_2dScatter->addDataPoints(outxyY, outQC, 2);
        d->m_2dScatter->addGamutOutline(outGamut, d->m_wtpt);
        d->m_container = d->m_2dScatter;
    }

    QSize screenSize = screen()->size();
    d->m_container->setMinimumSize(QSize(screenSize.height() / 1.3, screenSize.height() / 1.3));
    d->m_container->setMaximumSize(screenSize);
    d->m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d->m_container->setFocusPolicy(Qt::StrongFocus);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QVBoxLayout *subLayout = new QVBoxLayout();
    QHBoxLayout *sub2Layout = new QHBoxLayout();

    mainLayout->addWidget(d->m_container, 1);

    mainLayout->addLayout(subLayout);

    this->setAttribute(Qt::WA_DeleteOnClose, true);
    this->setWindowTitle(QStringLiteral("Plotting result"));

    QPushButton *saveAsImgBtn = new QPushButton(this);
    saveAsImgBtn->setText(QStringLiteral("Save as image..."));

    // detach these to prevent mucking 2d plot
    QPushButton *resetCamBtn = new QPushButton();
    if (!d->m_is2d) {
        resetCamBtn->setText(QStringLiteral("Top-down camera"));
    } else {
        resetCamBtn->setText(QStringLiteral("Reset view"));
    }
    QCheckBox *orthoChk = new QCheckBox();
    orthoChk->setChecked(false);
    orthoChk->setText("Orthogonal projection");

    const QString imLabel = [&]() {
        QString tmp = "<b>File name:</b> " + d->m_fName + "<br><b>Profile name:</b> ";
        if (!d->m_profileName.isEmpty()) {
            tmp += d->m_profileName;
        } else {
            tmp += "None (Assumed as sRGB)";
        }
        tmp += "<br><b>Profile white:</b> ";
        const QVector2D wtpt = d->m_wtpt;
        tmp += "x: " + QString::number(wtpt.x()) + " | y: " + QString::number(wtpt.y());
        return tmp;
    }();
    QLabel *labelName = new QLabel(this);
    labelName->setTextFormat(Qt::RichText);
    labelName->setText(imLabel);
    subLayout->addWidget(labelName, 0, Qt::AlignLeft);
    if (!d->m_is2d) {
        subLayout->addWidget(orthoChk, 0, Qt::AlignLeft);
    }

    subLayout->addLayout(sub2Layout);
    sub2Layout->addWidget(resetCamBtn, 0, Qt::AlignCenter);
    sub2Layout->addWidget(saveAsImgBtn, 0, Qt::AlignCenter);

    QObject::connect(saveAsImgBtn, &QPushButton::clicked, this, &ScatterDialog::saveButtonPress);

    if (!d->m_is2d) {
        QObject::connect(resetCamBtn, &QPushButton::clicked, d->m_3dScatter, &Scatter3dChart::changePresetCamera);
        QObject::connect(orthoChk, &QCheckBox::clicked, d->m_3dScatter, &Scatter3dChart::setOrthogonal);
    } else {
        QObject::connect(resetCamBtn, &QPushButton::clicked, d->m_2dScatter, &Scatter2dChart::resetCamera);
    }
}

ScatterDialog::~ScatterDialog()
{
    delete d;
}

void ScatterDialog::saveButtonPress()
{
    const QString tmpFileName = QFileDialog::getSaveFileName(this,
                                                             tr("Save plot as image"),
                                                             QDir::currentPath(),
                                                             tr("Portable Network Graphics (*.png)"));
    if (tmpFileName.isEmpty()) {
        return;
    }

    const auto useLabel =
        QMessageBox::question(this, "Save label", "Do you want to include filename and profile label attached?");

    QFileInfo info(d->m_fName);

    QString chTitle(info.fileName());
    chTitle += "\n";
    if (!d->m_profileName.isEmpty()) {
        chTitle += d->m_profileName;
    } else {
        chTitle += "None (Assumed as sRGB)";
    }

    // QImage out = d->m_3dScatter->renderToImage(8);

    QPixmap rend(d->m_container->size());
    d->m_container->render(&rend);
    QImage out = rend.toImage();

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
