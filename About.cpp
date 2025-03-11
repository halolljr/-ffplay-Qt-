#include "About.h"
#include "GlobalHelper.h"

About::About(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::AboutClass())
{
	ui->setupUi(this);
}

About::~About()
{
	delete ui;
}

bool About::Init()
{
	//弹出关于窗口（将窗口设置为模态窗口），主窗口不可点击
	this->setWindowModality(Qt::ApplicationModal); 

	//this->setWindowFlags(Qt::Dialog);
	//setWindowFlags(Qt::FramelessWindowHint /*| Qt::WindowSystemMenuHint*/ | Qt::WindowMinimizeButtonHint);

	this->setWindowIcon(QIcon(":/Player/res/player.png"));
	ui->LogoLabel->setPixmap(QPixmap(":/Player/res/player.png").scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));

	//setStyleSheet(GlobalHelper::GetQssStr("://res/qss/about.css"));

	QString strVersion = QString("版本：%1\n时间：%2").arg(GlobalHelper::GetAppVersion()).arg(QString(__DATE__) + " " + QString(__TIME__));
	ui->VersionLabel->setText(strVersion);


	return true;
}



void About::mousePressEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		m_bMoveDrag = true;
		/*鼠标全局位置-窗口当前位置*/
		m_DragPosition = event->globalPos() - this->pos();
	}

	QWidget::mousePressEvent(event);
}

void About::mouseReleaseEvent(QMouseEvent* event)
{
	m_bMoveDrag = false;

	QWidget::mouseReleaseEvent(event);
}

void About::mouseMoveEvent(QMouseEvent* event)
{
	if (m_bMoveDrag)
	{
		move(event->globalPos() - m_DragPosition);
	}

	QWidget::mouseMoveEvent(event);
}


void About::on_ClosePushButton_clicked()
{
	hide();
}
