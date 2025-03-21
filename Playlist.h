#pragma once

#include <QWidget>
#include <QListWidgetItem>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include "ui_Playlist.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PlaylistClass; };
QT_END_NAMESPACE

class Playlist : public QWidget
{
	Q_OBJECT

public:
	Playlist(QWidget *parent = nullptr);
	~Playlist();
    bool Init();
    /**
     * @brief	获取播放列表状态
     *
     * @return	true 显示 false 隐藏
     * @note
     */
    bool GetPlaylistStatus();
    int GetCurrentIndex();
public:
    /**
     * @brief	添加文件（在播放栏处点击）
     *
     * @param	strFileName 文件完整路径
     * @note
     */
    void OnAddFile(QString strFileName);
    /// <summary>
    /// 添加并开始播放，在标题栏/展示页面/CtrlBar处点击
    /// </summary>
    /// <param name="strFileName"></param>
    void OnAddFileAndPlay(QString strFileName);
    /// <summary>
    /// 与CtrlBar::SigBackwardPlay连接，上一个文件
    /// </summary>
    void OnBackwardPlay();
    /// <summary>
    /// 与CtrlBar::SigForwardPlay连接，下一个文件
    /// </summary>
    void OnForwardPlay();

    /* 在这里定义dock的初始大小 */
    QSize sizeHint() const
    {
        return QSize(150, 900);
    }
protected:
    /**
    * @brief	放下事件（支持拖动文件到文件列表）
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

signals:
    void SigUpdateUi();	//< 界面排布更新
    /// <summary>
    /// 与Show::SigPlay连接
    /// </summary>
    /// <param name="strFile"></param>
    void SigPlay(QString strFile); //< 播放文件
private:
    bool InitUi();
    bool ConnectSignalSlots();
private slots:
    /// <summary>
    /// 内部会发送SigPlay信号
    /// </summary>
    void on_List_itemDoubleClicked(QListWidgetItem* item);
private:
	Ui::PlaylistClass *ui;
    int m_nCurrentPlayListIndex;
};
