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
	/// 主要处理窗口移动
	/// </summary>
	/// <param name="event">鼠标事件对象</param>
	bool Init();
protected:

	void mousePressEvent(QMouseEvent* event);
	void mouseReleaseEvent(QMouseEvent* event);
	void mouseMoveEvent(QMouseEvent* event);
private slots:
	void on_ClosePushButton_clicked();
private:
	Ui::AboutClass *ui;
	bool m_bMoveDrag;//移动窗口标志
	QPoint m_DragPosition;
};
