#include <QDebug>
#include <QMutex>

#include "Show.h"
#include "globalhelper.h"

#pragma execution_character_set("utf-8")

//在VideoCtrl会添加extern关键字
QMutex g_show_rect_mutex;

Show::Show(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::ShowClass()),
	m_stActionGroup(this),
	m_stMenu(this)
{
	ui->setupUi(this);
	//加载样式
	setStyleSheet(GlobalHelper::GetQssStr(":/Player/res/qss/show.css"));
	setAcceptDrops(true);

	//指示窗口部件在绘制时不需要清除背景，即窗口部件完全负责其区域的绘制。
	//这样可以避免在重绘时清除背景，减少闪烁，防止过度刷新显示。
	//防止过度刷新显示
	this->setAttribute(Qt::WA_OpaquePaintEvent);
	//ui->label->setAttribute(Qt::WA_OpaquePaintEvent);
	
	//禁用了 ui->label 部件的更新
	//部件将不会重绘或接收绘制事件。这在需要对部件进行多次修改但不希望每次修改都触发重绘时非常有用。
	ui->label->setUpdatesEnabled(false);
	//即使未按下鼠标按钮，窗口部件也会接收鼠标移动事件
	this->setMouseTracking(true);

	m_nLastFrameWidth = 0; ///< 记录视频宽高
	m_nLastFrameHeight = 0;

	m_stActionGroup.addAction("全屏");
	m_stActionGroup.addAction("暂停");
	m_stActionGroup.addAction("停止");

	m_stMenu.addActions(m_stActionGroup.actions());
}

Show::~Show()
{
	delete ui;
}
bool Show::Init()
{
	if (ConnectSignalSlots() == false)
	{
		return false;
	}

	//ui->label->setUpdatesEnabled(false);
	return true;
}

void Show::OnFrameDimensionsChanged(int nFrameWidth, int nFrameHeight)
{
	qDebug() << "Show::OnFrameDimensionsChanged" << nFrameWidth << nFrameHeight;
	m_nLastFrameWidth = nFrameWidth;
	m_nLastFrameHeight = nFrameHeight;

	ChangeShow();
}

void Show::ChangeShow()
{
	g_show_rect_mutex.lock();
	//没有有效帧
	if (m_nLastFrameWidth == 0 && m_nLastFrameHeight == 0)
	{
		// label 的显示区域设置为窗口的整个区域。
		ui->label->setGeometry(0, 0, width(), height());
	}
	else
	{
		//帧的宽高比
		float aspect_ratio;
		//显示的宽度、高度、坐标值
		int width, height, x, y;
		//初始宽度
		int scr_width = this->width();
		//初始高度
		int scr_height = this->height();
		//计算帧的宽高比
		aspect_ratio = (float)m_nLastFrameWidth / (float)m_nLastFrameHeight;
		//显示高度
		height = scr_height;
		//显示宽度
		//确保宽度为偶数
		//lrint()：将上述计算结果四舍五入为最接近的长整数（long int)
		//将一个数与 ~1 进行按位与操作，会将该数的最低位置为 0，从而确保结果为偶数。
		width = lrint(height * aspect_ratio) & ~1;
		//显示宽度大于初始宽度
		if (width > scr_width)
		{
			width = scr_width;
			//因为aspect_ratio = (float)m_nLastFrameWidth / (float)m_nLastFrameHeight;
			height = lrint(width / aspect_ratio) & ~1;
		}
		//使其水平居中
		x = (scr_width - width) / 2;
		//使其垂直居中
		y = (scr_height - height) / 2;

		ui->label->setGeometry(x, y, width, height);
	}

	g_show_rect_mutex.unlock();
}

void Show::dragEnterEvent(QDragEnterEvent* event)
{
	//    if(event->mimeData()->hasFormat("text/uri-list"))
	//    {
	//        event->acceptProposedAction();
	//    }
	//部件将接受所有类型的拖动内容
	event->acceptProposedAction();
}

void Show::resizeEvent(QResizeEvent* event)
{
	Q_UNUSED(event);

	ChangeShow();
}

void Show::keyReleaseEvent(QKeyEvent* event)
{
	qDebug() << "Show::keyPressEvent:" << event->key();
	switch (event->key())
	{
	case Qt::Key_Return://全屏
		emit SigFullScreen();
		break;
	case Qt::Key_Left://后退5s
		emit SigSeekBack();
		break;
	case Qt::Key_Right://前进5s
		qDebug() << "前进5s";
		emit SigSeekForward();
		break;
	case Qt::Key_Up://增加10音量
		emit SigAddVolume();
		break;
	case Qt::Key_Down://减少10音量
		emit SigSubVolume();
		break;
	case Qt::Key_Space://播放或暂停
		emit SigPlayOrPause();
		break;
	default:
		QWidget::keyPressEvent(event);
		break;
	}
}

/*void Show::contextMenuEvent(QContextMenuEvent* event)
{
	m_stMenu.exec(event->globalPos());
	qDebug() << "Show::contextMenuEvent";
}*/

void Show::mousePressEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::RightButton)
	{
		emit SigShowMenu();
	}

	QWidget::mousePressEvent(event);
}

void Show::OnDisplayMsg(QString strMsg)
{
	qDebug() << "Show::OnDisplayMsg " << strMsg;
}

void Show::OnPlay(QString strFile)
{
	VideoCtl::GetInstance()->StartPlay(strFile, ui->label->winId());
}

void Show::OnStopFinished()
{
	update();
}


void Show::OnTimerShowCursorUpdate()
{
	//qDebug() << "Show::OnTimerShowCursorUpdate()";
	//setCursor(Qt::BlankCursor);
}

void Show::OnActionsTriggered(QAction* action)
{
	QString strAction = action->text();
	if (strAction == "全屏")
	{
		emit SigFullScreen();
	}
	else if (strAction == "停止")
	{
		emit SigStop();
	}
	else if (strAction == "暂停" || strAction == "播放")
	{
		emit SigPlayOrPause();
	}
}

bool Show::ConnectSignalSlots()
{
	QList<bool> listRet;
	bool bRet;

	bRet = connect(this, &Show::SigPlay, this, &Show::OnPlay);
	listRet.append(bRet);

	timerShowCursor.setInterval(2000);
	//设置定时器超时调用
	bRet = connect(&timerShowCursor, &QTimer::timeout, this, &Show::OnTimerShowCursorUpdate);
	listRet.append(bRet);

	connect(&m_stActionGroup, &QActionGroup::triggered, this, &Show::OnActionsTriggered);

	for (bool bReturn : listRet)
	{
		if (bReturn == false)
		{
			return false;
		}
	}

	return true;
}

void Show::dropEvent(QDropEvent* event)
{
	QList<QUrl> urls = event->mimeData()->urls();
	if (urls.isEmpty())
	{
		return;
	}

	for (QUrl url : urls)
	{
		QString strFileName = url.toLocalFile();
		qDebug() << strFileName;
		emit SigOpenFile(strFileName);
		break;
	}

	//emit SigPlay(urls.first().toLocalFile());
}
