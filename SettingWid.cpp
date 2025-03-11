#include "SettingWid.h"

SettingWid::SettingWid(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::SettingWidClass())
{
	ui->setupUi(this);
}

SettingWid::~SettingWid()
{
	delete ui;
}
