#include "camera3dsettingdialog.h"
#include "ui_camera3dsettingdialog.h"

#include <QVector3D>

Camera3DSettingDialog::Camera3DSettingDialog(PlotSetting3D &set, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Camera3DSettingDialog)
    , setting(set)
{
    ui->setupUi(this);

    ui->yawSpin->setValue(setting.yawAngle);
    ui->pitchSpin->setValue(setting.pitchAngle);

    ui->perspectiveCheck->setChecked(!setting.useOrtho);
    ui->fovSpin->setValue(setting.fov);

    ui->tgtXSpin->setValue(setting.targetPos.x());
    ui->tgtYSpin->setValue(setting.targetPos.y());
    ui->tgtZSpin->setValue(setting.targetPos.z());
    ui->distSpin->setValue(setting.camDistToTarget);

    resize(minimumSizeHint());
    setMinimumSize(minimumSizeHint());
    setMaximumSize(minimumSizeHint());
}

Camera3DSettingDialog::~Camera3DSettingDialog()
{
    delete ui;
}

PlotSetting3D &Camera3DSettingDialog::getSettings()
{
    setting.yawAngle = ui->yawSpin->value();
    setting.pitchAngle = ui->pitchSpin->value();
    setting.useOrtho = !ui->perspectiveCheck->isChecked();
    if (ui->perspectiveCheck->isChecked()) {
        setting.fov = ui->fovSpin->value();
    }
    setting.camDistToTarget = ui->distSpin->value();
    QVector3D sett{static_cast<float>(ui->tgtXSpin->value()),
                   static_cast<float>(ui->tgtYSpin->value()),
                   static_cast<float>(ui->tgtZSpin->value())};
    setting.targetPos = sett;

    return setting;
}
