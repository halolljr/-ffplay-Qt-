#pragma once

#include <QWidget>
#include "ui_SettingWid.h"

QT_BEGIN_NAMESPACE
namespace Ui { class SettingWidClass; };
QT_END_NAMESPACE

class SettingWid : public QWidget
{
	Q_OBJECT

public:
	SettingWid(QWidget *parent = nullptr);
	~SettingWid();

private:
	Ui::SettingWidClass *ui;
};
