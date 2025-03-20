#pragma once

#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>
/// <summary>
/// 在PlayList.UI直接将类提升为MedaiList类，而PlayList类在Player类中被设置为右侧DockWidget
/// </summary>
class MediaList  : public QListWidget
{
	Q_OBJECT

public:
	MediaList(QWidget* parent = 0);
	~MediaList();
	bool Init();
protected:
	/// <summary>
	/// 重写方法：用户在MediaList上右键点击时，菜单m_stMenu会在鼠标点击的位置弹出。
	/// </summary>
	/// <param name="event"></param>
	void contextMenuEvent(QContextMenuEvent* event);
private:
	void AddFile(); //添加文件
	void RemoveFile();
signals:
	/// <summary>
	/// 与Playlist::OnAddFile连接
	/// </summary>
	void SigAddFile(QString strFileName);   
private:
	QMenu m_stMenu;	//菜单表对象
	QAction m_stActAdd;     //添加文件
	QAction m_stActRemove;  //移除文件
	QAction m_stActClearList;//清空列表
};
