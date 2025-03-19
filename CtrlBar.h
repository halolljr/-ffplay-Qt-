#pragma once

#include <QWidget>
#include "CustomSlider.h"
#include "ui_CtrlBar.h"

QT_BEGIN_NAMESPACE
namespace Ui { class CtrlBarClass; };
QT_END_NAMESPACE

class CtrlBar : public QWidget
{
	Q_OBJECT

public:
    explicit CtrlBar(QWidget* parent = 0);
    ~CtrlBar();
    /// <summary>
    /// 初始化UI界面
    /// </summary>
    /// <returns>bool 成功与否</returns>
    bool Init();
public:
	/// <summary>
    /// 显示播放的总时长，槽函数，sender为VideoCtl的SigVideoTotalSeconds
    /// </summary>
    /// <param name="nSeconds">秒数</param>
    void OnVideoTotalSeconds(int nSeconds);
	/// <summary>
    /// 当前播放的时长，槽函数，sender为VideoCtl的SigVideoPlaySeconds
    /// </summary>
    /// <param name="nSeconds">秒数</param>
    void OnVideoPlaySeconds(int nSeconds);
	/// <summary>
	/// 更新界面层的音量显示，并存储在配置文件中，sender为VideoCtl的SigVideoVolume
	/// </summary>
	/// <param name="dPercent">音量系数</param>
    void OnVideopVolume(double dPercent);
	/// <summary>
   /// 播放或者暂停，sender为VideoCtl的SigPauseStat
   /// </summary>
   /// <param name="bPaused">0-非暂停</param>
    void OnPauseStat(bool bPaused);
	/// <summary>
   /// 终止播放，sender为VideoCtl的SigStopFinished
   /// </summary>
    void OnStopFinished();
	/// <summary>
	/// 更新播放速度，sender为VideoCtl的SigSpeed
	/// </summary>
	/// <param name="speed">float</param>
    void OnSpeed(float speed);

private:
    /// <summary>
    /// 与CustomSlider::SigCustomSliderValueChanged连接
    /// </summary>
    void OnPlaySliderValueChanged();
    void OnVolumeSliderValueChanged();

private slots:
    void on_PlayOrPauseBtn_clicked();
    /// <summary>
    /// 一键静音或者恢复到原来的音量
    /// </summary>
    void on_VolumeBtn_clicked();
    void on_StopBtn_clicked();
    void on_SettingBtn_clicked();
    /// <summary>
    /// 连接信号和槽函数
    /// </summary>
    /// <returns>bool 成功与否</returns>
    bool ConnectSignalSlots();
    void on_speedBtn_clicked();
signals:
    void SigShowOrHidePlaylist();	//< 显示或隐藏信号
    /// <summary>
    /// 与VideoCtl::OnPlaySeek连接
    /// </summary>
    /// <param name="dPercent">位置系数 double</param>
    void SigPlaySeek(double dPercent); 
    /// <summary>
    /// 与VideoCtl::OnPlayVolume连接
    /// </summary>
    /// <param name="dPercent">double 音量系数</param>
    void SigPlayVolume(double dPercent);
    /// <summary>
    /// 与VideoCtl::OnPause连接
    /// </summary>
    void SigPlayOrPause();
    /// <summary>
    /// 与VideoCtl::OnStop连接
    /// </summary>
    void SigStop();
    void SigForwardPlay();
    void SigBackwardPlay();
    void SigShowMenu();
    void SigShowSetting();
    /// <summary>
    /// 与VideoCtl::OnSpeed连接
    /// </summary>
    void SigSpeed();
private:
	Ui::CtrlBarClass *ui;
	int m_nTotalPlaySeconds;
	double m_dLastVolumePercent;    //最近更新的音量系数
};
