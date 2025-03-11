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
    /// 当前播放的时长，槽函数，sender为VideoCtlSig的VideoPlaySeconds
    /// </summary>
    /// <param name="nSeconds">秒数</param>
    void OnVideoPlaySeconds(int nSeconds);
	/// <summary>
	/// 更新播放的音量，并存储在配置文件中，sender为VideoCtl的SigVideoVolume
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
    void OnPlaySliderValueChanged();
    void OnVolumeSliderValueChanged();
private slots:
    void on_PlayOrPauseBtn_clicked();
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
    void SigPlaySeek(double dPercent); ///< 调整播放进度
    void SigPlayVolume(double dPercent);
    void SigPlayOrPause();
    void SigStop();
    void SigForwardPlay();
    void SigBackwardPlay();
    void SigShowMenu();
    void SigShowSetting();
    void SigSpeed();
private:
	Ui::CtrlBarClass *ui;
	int m_nTotalPlaySeconds;
	double m_dLastVolumePercent;
};
