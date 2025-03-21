#include <QFile>
#include <QPainter>
#include <QtMath>
#include <QDebug>
#include <QAbstractItemView>
#include <QMimeData>
#include <QSizeGrip>
#include <QWindow>
#include <QDesktopWidget>
#include <QScreen>
#include <QRect>
#include <QFileDialog>
#include "globalhelper.h"
#include "videoctl.h"
#include "Player.h"
const int FULLSCREEN_MOUSE_DETECT_TIME = 500;
Player::Player(QMainWindow *parent)
    : QMainWindow(parent)
	, ui(new Ui::PlayerClass()),
	m_nShadowWidth(0),
	m_stMenu(this),
	m_stPlaylist(this),
	m_stTitle(this),
	m_bMoveDrag(false),
	m_stActExit(this),
	m_stActAbout(this),
	m_stActOpen(this),
	m_stActFullscreen(this)
{
    ui->setupUi(this);
	//无边框、无系统菜单、 任务栏点击最小化
	//setWindowFlags(Qt::FramelessWindowHint /*| Qt::WindowSystemMenuHint*/ | Qt::WindowMinimizeButtonHint);
	 //设置任务栏图标
	this->setWindowIcon(QIcon(":/Player/res/player.png"));
	//加载样式
	QString qss = GlobalHelper::GetQssStr(":/Player/res/qss/mainwid.css");
	setStyleSheet(qss);
	// 追踪鼠标 用于播放时隐藏鼠标
	this->setMouseTracking(true);
	ui->ShowWid->setMouseTracking(true);
	//保证窗口不被绘制上的部分透明
	//setAttribute(Qt::WA_TranslucentBackground);
	//接受放下事件
	//setAcceptDrops(true);
	//可以清晰地看到放下过程中的图标指示
	//setDropIndicatorShown(true);
	//setAcceptDrops(true);
	//setDragDropMode(QAbstractItemView::DragDrop);
	//setDragEnabled(true);
	//setDropIndicatorShown(true);
	//窗口大小调节
	//QSizeGrip   *pSizeGrip = new QSizeGrip(this);
	//pSizeGrip->setMinimumSize(10, 10);
	//pSizeGrip->setMaximumSize(10, 10);
	//ui->verticalLayout->addWidget(pSizeGrip, 0, Qt::AlignBottom | Qt::AlignRight);
	m_bPlaying = false;
	m_bFullScreenPlay = false;
	m_stCtrlBarAnimationTimer.setInterval(2000);
	m_stFullscreenMouseDetectTimer.setInterval(FULLSCREEN_MOUSE_DETECT_TIME);
}

Player::~Player()
{
    delete ui;
}

bool Player::Init()
{
	//没有直接将类升级而是在此设置widget
	/*右侧播放列表*/
	QWidget* em = new QWidget(this);
	/*设置QDockWidget的标题栏为em*/
	ui->PlaylistWid->setTitleBarWidget(em);
	/*设置QDockWidget的内容为m_stPlaylist*/
	ui->PlaylistWid->setWidget(&m_stPlaylist);
	//ui->PlaylistWid->setFixedWidth(100);

	QWidget* emTitle = new QWidget(this);
	/*上方标题栏*/
	/*设置QDockWidget的标题栏为emTitle*/
	ui->TitleWid->setTitleBarWidget(emTitle);
	/*设置QDockWidget的内容为m_stTitle*/
	ui->TitleWid->setWidget(&m_stTitle);
	
	//FramelessHelper *pHelper = new FramelessHelper(this); //无边框管理
	//pHelper->activateOn(this);  //激活当前窗体
	//pHelper->setTitleHeight(ui->TitleWid->height());  //设置窗体的标题栏高度
	//pHelper->setWidgetMovable(true);  //设置窗体可移动
	//pHelper->setWidgetResizable(true);  //设置窗体可缩放
	//pHelper->setRubberBandOnMove(true);  //设置橡皮筋效果-可移动
	//pHelper->setRubberBandOnResize(true);  //设置橡皮筋效果-可缩放
	//连接自定义信号与槽
	if (ConnectSignalSlots() == false)
	{
		return false;
	}
	/*
		CtrlBarWid：播放控制（类提升）
		ShowWid：播放界面（类提升），即使show类没有重写contextMenuEvent，且在全屏的时候为独立窗口焦点，contextMenuEvent也有效
	*/
	if (ui->CtrlBarWid->Init() == false ||
		m_stPlaylist.Init() == false ||
		ui->ShowWid->Init() == false ||
		m_stTitle.Init() == false)
	{
		return false;
	}
	//显示和隐藏控制栏（CtrlBarWid）时的动画效果
	m_stCtrlbarAnimationShow = new QPropertyAnimation(ui->CtrlBarWid, "geometry");
	m_stCtrlbarAnimationHide = new QPropertyAnimation(ui->CtrlBarWid, "geometry");
	if (m_stAboutWidget.Init() == false)
	{
		return false;
	}
	m_stActFullscreen.setText("全屏");
	m_stActFullscreen.setCheckable(true);
	m_stMenu.addAction(&m_stActFullscreen);

	m_stActOpen.setText("打开");
	m_stMenu.addAction(&m_stActOpen);
	
	m_stActAbout.setText("关于");
	m_stMenu.addAction(&m_stActAbout);
	
	m_stActExit.setText("退出");
	m_stMenu.addAction(&m_stActExit);
	return true;
}

void  Player::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
}


void  Player::enterEvent(QEvent* event)
{
	Q_UNUSED(event);
}

void  Player::leaveEvent(QEvent* event)
{
	Q_UNUSED(event);
}

bool  Player::ConnectSignalSlots()
{
	connect(&m_stTitle, &Title::SigCloseBtnClicked, this, &Player::OnCloseBtnClicked);
	connect(&m_stTitle, &Title::SigMaxBtnClicked, this, &Player::OnMaxBtnClicked);
	connect(&m_stTitle, &Title::SigMinBtnClicked, this, &Player::OnMinBtnClicked);
	connect(&m_stTitle, &Title::SigDoubleClicked, this, &Player::OnMaxBtnClicked);
	connect(&m_stTitle, &Title::SigFullScreenBtnClicked, this, &Player::OnFullScreenPlay);
	connect(&m_stTitle, &Title::SigOpenFile, &m_stPlaylist, &Playlist::OnAddFileAndPlay);
	connect(&m_stTitle, &Title::SigShowMenu, this, &Player::OnShowMenu);

	connect(&m_stPlaylist, &Playlist::SigPlay, ui->ShowWid, &Show::SigPlay);

	connect(ui->ShowWid, &Show::SigOpenFile, &m_stPlaylist, &Playlist::OnAddFileAndPlay);
	connect(ui->ShowWid, &Show::SigFullScreen, this, &Player::OnFullScreenPlay);
	connect(ui->ShowWid, &Show::SigPlayOrPause, VideoCtl::GetInstance(), &VideoCtl::OnPause);
	connect(ui->ShowWid, &Show::SigStop, VideoCtl::GetInstance(), &VideoCtl::OnStop);
	connect(ui->ShowWid, &Show::SigShowMenu, this, &Player::OnShowMenu);
	connect(ui->ShowWid, &Show::SigSeekForward, VideoCtl::GetInstance(), &VideoCtl::OnSeekForward);
	connect(ui->ShowWid, &Show::SigSeekBack, VideoCtl::GetInstance(), &VideoCtl::OnSeekBack);
	connect(ui->ShowWid, &Show::SigAddVolume, VideoCtl::GetInstance(), &VideoCtl::OnAddVolume);
	connect(ui->ShowWid, &Show::SigSubVolume, VideoCtl::GetInstance(), &VideoCtl::OnSubVolume);

	connect(ui->CtrlBarWid, &CtrlBar::SigSpeed, VideoCtl::GetInstance(), &VideoCtl::OnSpeed);
	connect(ui->CtrlBarWid, &CtrlBar::SigShowOrHidePlaylist, this, &Player::OnShowOrHidePlaylist);
	connect(ui->CtrlBarWid, &CtrlBar::SigPlaySeek, VideoCtl::GetInstance(), &VideoCtl::OnPlaySeek);
	connect(ui->CtrlBarWid, &CtrlBar::SigPlayVolume, VideoCtl::GetInstance(), &VideoCtl::OnPlayVolume);
	connect(ui->CtrlBarWid, &CtrlBar::SigPlayOrPause, VideoCtl::GetInstance(), &VideoCtl::OnPause);
	connect(ui->CtrlBarWid, &CtrlBar::SigStop, VideoCtl::GetInstance(), &VideoCtl::OnStop);
	connect(ui->CtrlBarWid, &CtrlBar::SigBackwardPlay, &m_stPlaylist, &Playlist::OnBackwardPlay);
	connect(ui->CtrlBarWid, &CtrlBar::SigForwardPlay, &m_stPlaylist, &Playlist::OnForwardPlay);
	connect(ui->CtrlBarWid, &CtrlBar::SigShowMenu, this, &Player::OnShowMenu);
	connect(ui->CtrlBarWid, &CtrlBar::SigShowSetting, this, &Player::OnShowSettingWid);

	connect(this, &Player::SigShowMax, &m_stTitle, &Title::OnChangeMaxBtnStyle);
	connect(this, &Player::SigSeekForward, VideoCtl::GetInstance(), &VideoCtl::OnSeekForward);
	connect(this, &Player::SigSeekBack, VideoCtl::GetInstance(), &VideoCtl::OnSeekBack);
	connect(this, &Player::SigAddVolume, VideoCtl::GetInstance(), &VideoCtl::OnAddVolume);
	connect(this, &Player::SigSubVolume, VideoCtl::GetInstance(), &VideoCtl::OnSubVolume);
	connect(this, &Player::SigOpenFile, &m_stPlaylist, &Playlist::OnAddFile);

	connect(VideoCtl::GetInstance(), &VideoCtl::SigSpeed, ui->CtrlBarWid, &CtrlBar::OnSpeed);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoTotalSeconds, ui->CtrlBarWid, &CtrlBar::OnVideoTotalSeconds);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoPlaySeconds, ui->CtrlBarWid, &CtrlBar::OnVideoPlaySeconds);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoVolume, ui->CtrlBarWid, &CtrlBar::OnVideopVolume);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigPauseStat, ui->CtrlBarWid, &CtrlBar::OnPauseStat, Qt::QueuedConnection);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigStopFinished, ui->CtrlBarWid, &CtrlBar::OnStopFinished, Qt::QueuedConnection);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigStopFinished, ui->ShowWid, &Show::OnStopFinished, Qt::QueuedConnection);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigFrameDimensionsChanged, ui->ShowWid, &Show::OnFrameDimensionsChanged, Qt::QueuedConnection);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigStopFinished, &m_stTitle, &Title::OnStopFinished, Qt::DirectConnection);
	connect(VideoCtl::GetInstance(), &VideoCtl::SigStartPlay, &m_stTitle, &Title::OnPlay, Qt::DirectConnection);

	connect(&m_stCtrlBarAnimationTimer, &QTimer::timeout, this, &Player::OnCtrlBarAnimationTimeOut);
	connect(&m_stFullscreenMouseDetectTimer, &QTimer::timeout, this, &Player::OnFullscreenMouseDetectTimeOut);

	connect(&m_stActAbout, &QAction::triggered, this, &Player::OnShowAbout);
	connect(&m_stActFullscreen, &QAction::triggered, this, &Player::OnFullScreenPlay);
	connect(&m_stActExit, &QAction::triggered, this, &Player::OnCloseBtnClicked);
	connect(&m_stActOpen, &QAction::triggered, this, &Player::OpenFile);

	return true;
}


void  Player::keyReleaseEvent(QKeyEvent* event)
{
	// 	    // 是否按下Ctrl键      特殊按键
	//     if(event->modifiers() == Qt::ControlModifier){
	//         // 是否按下M键    普通按键  类似
	//         if(event->key() == Qt::Key_M)
	//             ···
	//     }
	qDebug() << "MainWid::keyPressEvent:" << event->key();
	switch (event->key())
	{
	case Qt::Key_Return://全屏
		OnFullScreenPlay();
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
	case Qt::Key_Space://暂停或者继续播放
		emit SigPlayOrPause();
		break;
	default:
		break;
	}
}


void  Player::mousePressEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		if (ui->TitleWid->geometry().contains(event->pos()))
		{
			m_bMoveDrag = true;
			m_DragPosition = event->globalPos() - this->pos();
		}
	}
	QWidget::mousePressEvent(event);
}

void  Player::mouseReleaseEvent(QMouseEvent* event)
{
	m_bMoveDrag = false;
	QWidget::mouseReleaseEvent(event);
}

void  Player::mouseMoveEvent(QMouseEvent* event)
{
	if (m_bMoveDrag)
	{
		move(event->globalPos() - m_DragPosition);
	}
	QWidget::mouseMoveEvent(event);
}

void  Player::contextMenuEvent(QContextMenuEvent* event)
{
	m_stMenu.exec(event->globalPos());
	event->accept();
}

void Player::OnFullScreenPlay()
{
	if (m_bFullScreenPlay == false)
	{
		//将成员变量m_bFullScreenPlay设为true，表示进入全屏模式
		m_bFullScreenPlay = true;
		//全屏操作的菜单项m_stActFullscreen设为选中状态。
		m_stActFullscreen.setChecked(true);
		//将显示窗口设置为独立窗口，以便全屏显示
		ui->ShowWid->setWindowFlags(Qt::Window);
		//多屏情况下，在当前屏幕全屏
		QScreen* pStCurScreen = qApp->screens().at(qApp->desktop()->screenNumber(this));
		//将显示窗口ShowWid的屏幕设置为当前屏幕。
		ui->ShowWid->windowHandle()->setScreen(pStCurScreen);
		// 全屏显示视频窗口
		//导致窗口状态的变化，从而可能触发与窗口状态相关的信号。
		ui->ShowWid->showFullScreen();
		//获取屏幕的几何尺寸和控制栏的高度，计算控制栏在显示和隐藏时的位置。
		QRect stScreenRect = pStCurScreen->geometry();
		int nCtrlBarHeight = ui->CtrlBarWid->height();
		int nX = ui->ShowWid->x();
		// // 设置控制栏显示和隐藏时的目标区域
		m_stCtrlBarAnimationShow = QRect(nX, stScreenRect.height() - nCtrlBarHeight, stScreenRect.width(), nCtrlBarHeight);
		m_stCtrlBarAnimationHide = QRect(nX, stScreenRect.height(), stScreenRect.width(), nCtrlBarHeight);
		// 配置控制栏显示动画
		m_stCtrlbarAnimationShow->setStartValue(m_stCtrlBarAnimationHide);
		m_stCtrlbarAnimationShow->setEndValue(m_stCtrlBarAnimationShow);
		m_stCtrlbarAnimationShow->setDuration(1000);
		// 配置控制栏隐藏动画
		m_stCtrlbarAnimationHide->setStartValue(m_stCtrlBarAnimationShow);
		m_stCtrlbarAnimationHide->setEndValue(m_stCtrlBarAnimationHide);
		m_stCtrlbarAnimationHide->setDuration(1000);
		// 将控制栏设置为无边框独立窗口，确保其显示在全屏视频上方
		ui->CtrlBarWid->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
		ui->CtrlBarWid->windowHandle()->setScreen(pStCurScreen);
		ui->CtrlBarWid->raise();
		ui->CtrlBarWid->setWindowOpacity(0.5);
		ui->CtrlBarWid->showNormal();
		ui->CtrlBarWid->windowHandle()->setScreen(pStCurScreen);
		// 启动控制栏显示动画
		m_stCtrlbarAnimationShow->start();
		m_bFullscreenCtrlBarShow = true;
		m_stFullscreenMouseDetectTimer.start();
		//调用this->setFocus()，会出现无法"按下回车键以退出的情况"
		//this->setFocus();
		//activateWindow() 函数用于将窗口设置为活动窗口，确保其能够接收用户输入和事件。
		ui->ShowWid->activateWindow();
		//setFocus() 函数用于将键盘焦点设置到指定的控件，使其能够接收键盘事件
		ui->ShowWid->setFocus();
	}
	else
	{
		// 退出全屏模式
		m_bFullScreenPlay = false;
		m_stActFullscreen.setChecked(false);
		// 停止控制栏动画，防止快速切换时出现问题
		m_stCtrlbarAnimationShow->stop(); //快速切换时，动画还没结束导致控制面板消失
		m_stCtrlbarAnimationHide->stop();
		// 恢复控制栏为子窗口模式
		ui->CtrlBarWid->setWindowOpacity(1);
		ui->CtrlBarWid->setWindowFlags(Qt::SubWindow);
		// 恢复显示窗口为子窗口模式
		ui->ShowWid->setWindowFlags(Qt::SubWindow);
		// 显示控制栏和视频窗口
		ui->CtrlBarWid->showNormal();
		ui->ShowWid->showNormal();
		// 停止全屏鼠标检测计时器
		m_stFullscreenMouseDetectTimer.stop();
		this->setFocus();
	}
}

void  Player::OnCtrlBarAnimationTimeOut()
{
	QApplication::setOverrideCursor(Qt::BlankCursor);
}

void  Player::OnFullscreenMouseDetectTimeOut()
{
	//     qDebug() << m_stCtrlBarAnimationShow;
	//     qDebug() << cursor().pos();
	//     qDebug() << ui->CtrlBarWid->geometry();
	if (m_bFullScreenPlay)
	{
		if (m_stCtrlBarAnimationShow.contains(cursor().pos()))
		{
			//判断鼠标是否在控制面板上面
			if (ui->CtrlBarWid->geometry().contains(cursor().pos()))
			{
				//继续显示
				m_bFullscreenCtrlBarShow = true;
			}
			else
			{
				//需要显示
				ui->CtrlBarWid->raise();

				m_stCtrlbarAnimationShow->start();
				m_stCtrlbarAnimationHide->stop();
				stCtrlBarHideTimer.stop();
			}
		}
		else
		{
			if (m_bFullscreenCtrlBarShow)
			{
				//需要隐藏
				m_bFullscreenCtrlBarShow = false;
				stCtrlBarHideTimer.singleShot(2000, this, &Player::OnCtrlBarHideTimeOut);
			}

		}

	}
}

void Player::OnCtrlBarHideTimeOut()
{
	m_stCtrlbarAnimationHide->start();
}

void Player::OnShowMenu()
{
	m_stMenu.exec(cursor().pos());
}

void  Player::OnShowAbout()
{
	m_stAboutWidget.move(cursor().pos().x() - m_stAboutWidget.width() / 2, cursor().pos().y() - m_stAboutWidget.height() / 2);
	m_stAboutWidget.show();
}

void  Player::OpenFile()
{
	QString strFileName = QFileDialog::getOpenFileName(this, "打开文件", QDir::homePath(),
		"视频文件(*.mkv *.rmvb *.mp4 *.avi *.flv *.wmv *.3gp)");
	emit SigOpenFile(strFileName);
}

void Player::OnShowSettingWid()
{
	m_stSettingWid.show();
}

void Player::OnCloseBtnClicked()
{
	this->close();
}

void Player::OnMinBtnClicked()
{
	this->showMinimized();
}

void Player::OnMaxBtnClicked()
{
	if (isMaximized())
	{
		showNormal();
		emit SigShowMax(false);
	}
	else
	{
		showMaximized();
		emit SigShowMax(true);
	}
}

void Player::OnShowOrHidePlaylist()
{
	if (ui->PlaylistWid->isHidden())
	{
		ui->PlaylistWid->show();
	}
	else
	{
		ui->PlaylistWid->hide();
	}
	this->repaint();        // 重绘主界面
}