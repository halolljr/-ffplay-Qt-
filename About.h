#pragma once
#include <QWidget>
#include <QMouseEvent>
#include <QPoint>
#include "ui_About.h"

QT_BEGIN_NAMESPACE
namespace Ui { class AboutClass; };
QT_END_NAMESPACE

class About : public QWidget
{
	Q_OBJECT
public:
	About(QWidget *parent = nullptr);
	~About();
	/// <summary>
	/// 设置成模态，无边框抽口，缺少标题栏，用户无法通过常规方式拖动窗口
	/// </summary>
	/// <returns></returns>
	bool Init();
protected:
	/// <summary>
	/// 主要处理窗口移动
	/// </summary>
	/// <param name="event"></param>
	void mousePressEvent(QMouseEvent* event);
	/// <summary>
	/// 拖拽事件结束
	/// </summary>
	/// <param name="event"></param>
	void mouseReleaseEvent(QMouseEvent* event);
	/// <summary>
	/// 计算窗口的新位置，并移动窗口
	/// 计算方式是：前后鼠标全局位置的偏移量+原窗口的左上角坐标
	/// </summary>
	/// <param name="event"></param>
	void mouseMoveEvent(QMouseEvent* event);
private slots:
	void on_ClosePushButton_clicked();
private:
	Ui::AboutClass *ui;
	bool m_bMoveDrag;//移动窗口标志
	QPoint m_DragPosition;
};
