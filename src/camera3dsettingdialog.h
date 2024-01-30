#ifndef CAMERA3DSETTINGDIALOG_H
#define CAMERA3DSETTINGDIALOG_H

#include <QDialog>

#include "plot_typedefs.h"

namespace Ui
{
class Camera3DSettingDialog;
}

class Camera3DSettingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Camera3DSettingDialog(PlotSetting3D &set, QWidget *parent = nullptr);
    ~Camera3DSettingDialog();

    void extracted();
    PlotSetting3D &getSettings();

private:
    Ui::Camera3DSettingDialog *ui;
    PlotSetting3D setting;
};

#endif // CAMERA3DSETTINGDIALOG_H
