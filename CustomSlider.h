#pragma once

#include <QSlider>
#include <QMouseEvent>
class CustomSlider  : public QSlider
{
	Q_OBJECT

public:
	CustomSlider(QWidget* parent);
	~CustomSlider();
protected:
	/// <summary>
	/// 重写QSlider的mousePressEvent事件:按下的时候
	/// </summary>
	/// <param name="ev">鼠标事件</param>
	void mousePressEvent(QMouseEvent* ev);
	/// <summary>
	/// 重写QSlider的mousePressEvent事件:松开的时候
	/// </summary>
	/// <param name="ev"></param>
	void mouseReleaseEvent(QMouseEvent* ev);
	/// <summary>
	/// 重写QSlider的mousePressEvent事件:鼠标移动的时候
	/// </summary>
	/// <param name="ev"></param>
	void mouseMoveEvent(QMouseEvent* ev);
signals:
	/// <summary>
	/// 自定义的鼠标单击信号，用于捕获并处理
	/// </summary>
	void SigCustomSliderValueChanged();
};
