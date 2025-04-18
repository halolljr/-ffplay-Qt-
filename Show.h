﻿#pragma once

#include <QMimeData>
#include <QDebug>
#include <QTimer>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QActionGroup>
#include <QAction>
#include <QWidget>
#include "videoctl.h"
#include "ui_Show.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ShowClass; };
QT_END_NAMESPACE

class Show : public QWidget
{
	Q_OBJECT

public:
	Show(QWidget *parent = nullptr);
	~Show();
    /**
      * @brief	初始化
      */
    bool Init();

protected:
    /**
     * @brief	放下事件
     *
     * @param	event 事件指针
     * @note
     */
    void dropEvent(QDropEvent* event);
    /**
     * @brief	拖动事件
     *
     * @param	event 事件指针
     * @note
     */
    void dragEnterEvent(QDragEnterEvent* event);
    /**
     * @brief	窗口大小变化事件
     *
     * @param	event 事件指针
     * @note
     */
    void resizeEvent(QResizeEvent* event);

    /**
     * @brief	按键事件
     *
     * @param
     * @return
     * @note
     */
    void keyReleaseEvent(QKeyEvent* event);


    void mousePressEvent(QMouseEvent* event);
    //void contextMenuEvent(QContextMenuEvent* event);
public:
    /**
    * @brief	播放
    *
    * @param	strFile 文件名
    * @note
    */
    void OnPlay(QString strFile);
    void OnStopFinished();

    /**
     * @brief	调整显示画面的宽高，使画面保持原比例
     *
     * @param	nFrameWidth 宽
     * @param	nFrameHeight 高
     * @return
     * @note
     */
    void OnFrameDimensionsChanged(int nFrameWidth, int nFrameHeight);
private:
    /**
     * @brief	显示信息
     *
     * @param	strMsg 信息内容
     * @note
     */
    void OnDisplayMsg(QString strMsg);


    void OnTimerShowCursorUpdate();

    void OnActionsTriggered(QAction* action);
private:
    /**
     * @brief	连接信号槽
     */
    bool ConnectSignalSlots();

    /// <summary>
    /// 一旦帧的宽高变了或者显示窗口变化了，主要是让label标签居中
    /// </summary>
    void ChangeShow();
signals:
    void SigOpenFile(QString strFileName);///< 增加视频文件
    void SigPlay(QString strFile); ///<播放

    void SigFullScreen();//全屏播放
    void SigPlayOrPause();
    void SigStop();
    //与Player::OnShowMenu连接
    void SigShowMenu();

    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();
private:
	Ui::ShowClass *ui;

	int m_nLastFrameWidth; ///< 记录视频宽高
	int m_nLastFrameHeight;

	QTimer timerShowCursor;

	QMenu m_stMenu;
	QActionGroup m_stActionGroup;
};
