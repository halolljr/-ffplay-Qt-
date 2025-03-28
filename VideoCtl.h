#pragma once

#include <QObject>
#include <QThread>
#include <QString>
#include <cmath>
#include "globalhelper.h"
#include "datactl.h"
#include "sonic.h"
#define FFP_PROP_FLOAT_PLAYBACK_RATE                    10003       // 设置播放速率
#define FFP_PROP_FLOAT_PLAYBACK_VOLUME                  10006

#define PLAYBACK_RATE_MIN           0.25     // 最慢
#define PLAYBACK_RATE_MAX           3.0     // 最快
#define PLAYBACK_RATE_SCALE         0.25    // 变速刻度
//单例模式
class VideoCtl : public QObject
{
    Q_OBJECT

public:
    //explicit VideoCtl(QObject *parent = nullptr);
    static VideoCtl* GetInstance();
    ~VideoCtl();
    /**
   * @brief	开始播放
   *
   * @param	strFileName 文件完整路径
   * @param	widPlayWid 传递winId()的目的是使外部视频渲染引擎能够识别并在指定的Qt窗口部件上进行渲染，从而实现视频内容在Qt应用程序中的显示
   * @return	true 成功 false 失败
   * @note
   */
    bool StartPlay(QString strFileName, WId widPlayWid);
	/// <summary>
    /// 实现了从帧队列中取数据、必要的重采样处理以及音频时钟的更新，为后续音频播放和同步提供数据。
    /// </summary>
    /// <param name="is"></param>
    /// <returns>重采样后的数据大小（或直接返回原始数据大小，如果没有重采样）</returns>
    int audio_decode_frame(VideoState* is);
    void update_sample_display(VideoState* is, short* samples, int samples_size);
	/// <summary>
    /// 设置Clock的各项属性
    /// </summary>
    /// <param name="c">：is->Clock</param>
    /// <param name="pts"></param>
    /// <param name="serial"></param>
    /// <param name="time">当前时间</param>
    void set_clock_at(Clock* c, double pts, int serial, double time);
    void sync_clock_to_slave(Clock* c, Clock* slave);
    float     ffp_get_property_float(int id, float default_value);
    void      ffp_set_property_float(int id, float value);
    //    int64_t   ffp_get_property_int64(int id, int64_t default_value);
    //    void      ffp_set_property_int64(int id, int64_t value);
signals:
    void SigSpeed(float speed);
    void SigPlayMsg(QString strMsg);//< 错误信息
    void SigFrameDimensionsChanged(int nFrameWidth, int nFrameHeight); //<视频宽高发生变化
    /// <summary>
    /// 信号，和CtrlBar::OnVideoTotalSeconds连接
    /// </summary>
    /// <param name="nSeconds">秒为单位</param>
    void SigVideoTotalSeconds(int nSeconds);
    void SigVideoPlaySeconds(int nSeconds);
    /// <summary>
    /// 信号；与CtrlBar::OnVideopVolume连接
    /// </summary>
    /// <param name="dPercent">0-1的区间范围</param>
    void SigVideoVolume(double dPercent);
    /// <summary>
    /// 信号；与CtrlBar::OnPauseStat连接
    /// </summary>
    /// <param name="bPaused">0-非暂停</param>
    void SigPauseStat(bool bPaused);
    void SigStop();
    void SigStopFinished();//停止播放完成
    void SigStartPlay(QString strFileName);
public:
    void OnSpeed();
    /// <summary>
    /// 与SigPlaySeek连接
    /// </summary>
    /// <param name="dPercent"></param>
    void OnPlaySeek(double dPercent);
    void OnPlayVolume(double dPercent);
    /// <summary>
    /// 与Player::SigSeekForward连接，前进X（5）秒
    /// </summary>
    void OnSeekForward();
    /// <summary>
    /// 与Player::SigSeekBack连接，后退X（5）秒
    /// </summary>
    void OnSeekBack();
    /// <summary>
    /// 与Player::SigAddVolume连接，增加Y（10）音量
    /// </summary>
    void OnAddVolume();
    /// <summary>
    /// 与Player::SigSubVolume连接，降低Y（10）音量
    /// </summary>
    void OnSubVolume();
    void OnPause();
    void OnStop();
private:
    explicit VideoCtl(QObject* parent = nullptr);
    /**
     * @brief	初始化
     *
     * @return	true 成功 false 失败
     * @note
     */
    bool Init();
    /**
     * @brief	连接信号槽
     *
     * @return	true 成功 false 失败
     * @note
     */
    bool ConnectSignalSlots();
    int get_video_frame(VideoState* is, AVFrame* frame);
    int audio_thread(void* arg);
    int video_thread(void* arg);
    int subtitle_thread(void* arg);
	/// <summary>
    /// 计算音频时钟与主时钟（通常是视频时钟或外部时钟）之间的差异，
    /// 然后根据这个差异决定是否需要增加或减少当前音频帧的采样数，
    /// 从而使音频播放速度轻微调整，达到音视频同步的目的。
    /// </summary>
    /// <param name="is"></param>
    /// <param name="nb_samples">原始采样数，如果不需要调整(即音频不为主时钟)，函数最终会返回这个值。</param>
    /// <returns></returns>
    int synchronize_audio(VideoState* is, int nb_samples);
    /// <summary>
    /// 根据预期，设置AudioParams结构体;其中重点是sdl读取音频数据的回调函数
    /// </summary>
    /// <param name="opaque">：VideoState*</param>
    /// <param name="wanted_channel_layout">预期的声道布局</param>
    /// <param name="wanted_nb_channels">预期的声道数</param>
    /// <param name="wanted_sample_rate">预期的采样率</param>
    /// <param name="audio_hw_params">SDL预期希望的音频结构体</param>
    /// <returns>SDL 打开的音频缓冲区的大小[size=samples×channels×bytes_per_sample]</returns>
    int audio_open(void* opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams* audio_hw_params);
    /// <summary>
    /// 初始化解码器上下文并打开解码器；开始对应的解码线程；
    /// </summary>
    /// <param name="is"></param>
    /// <param name="stream_index">流索引</param>
    /// <returns></returns>
    int stream_component_open(VideoState* is, int stream_index);
    int stream_has_enough_packets(AVStream* st, int stream_id, PacketQueue* queue);
    int is_realtime(AVFormatContext* s);
    /// <summary>
    /// 打开多媒体文件源；打开流；读取相应的AVPacket到相应的包队列中（读码）
    /// </summary>
    /// <param name="CurStream"></param>
    void ReadThread(VideoState* CurStream);
    void LoopThread(VideoState* CurStream);
    /// <summary>
    /// 总起函数，初始化AVPacket队列、AVFrame队列、初始化时钟等等，并开辟Readthread线程
    /// </summary>
    /// <param name="filename">：媒体源</param>
    /// <returns>VideoState*</returns>
    VideoState* stream_open(const char* filename);
    void stream_cycle_channel(VideoState* is, int codec_type);
    /// <summary>
    /// 在一个循环中持续检查事件队列，并根据需要刷新视频帧
    /// </summary>
    /// <param name="is"></param>
    /// <param name="event"></param>
    void refresh_loop_wait_event(VideoState* is, SDL_Event* event);
    void seek_chapter(VideoState* is, int incr);
    void video_refresh(void* opaque, double* remaining_time);
    int queue_picture(VideoState* is, AVFrame* src_frame, double pts, double duration, int64_t pos, int serial);
    //更新音量
    void UpdateVolume(int sign, double step);
    void video_display(VideoState* is);
    /// <summary>
    /// 创建SDL_Window和SDL_Render(会先尝试硬件渲染)，并将is->width设为显示空间的宽，is->height同理
    /// </summary>
    /// <param name="is"></param>
    /// <returns></returns>
    int video_open(VideoState* is);
    void do_exit(VideoState*& is);
    /// <summary>
    /// 如果SDL_Texture为空则创建一个；或者根据新的格式、宽度和高度，重新分配或更新传入的SDL_Texture对象。同时设置合适的混合模式，并在需要时初始化纹理（清零）。
    /// </summary>
    /// <param name="texture"></param>
    /// <param name="new_format">将AVFrame转化为SDL匹配的像素格式</param>
    /// <param name="new_width">帧的宽度</param>
    /// <param name="new_height">帧的高度</param>
    /// <param name="blendmode">为SDL_BLENDMODE_NONE</param>
    /// <param name="init_texture">为0</param>
    /// <returns></returns>
    int realloc_texture(SDL_Texture** texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
    /// <summary>
    /// 通过上一待显示（第一帧）的帧或者上一已经显示的帧来计算视频在屏幕上的显示区域，以确保视频按照正确的宽高比进行显示。
    /// AVFrame中的 SAR（Sample Aspect Ratio即采样宽高比）用于描述单个像素的宽高比例，而视频帧的分辨率（pic_width 和 pic_height）只是像素的个数
    /// </summary>
    /// <param name="rect">SDL显示的矩形区域[x（左上角 x 坐标）、y（左上角 y 坐标）、w（宽度）和 h（高度）]</param>
    /// <param name="scr_xleft">：is->xleft</param>
    /// <param name="scr_ytop">：is->>ytop</param>
    /// <param name="scr_width">：is->width</param>
    /// <param name="scr_height">：is->height</param>
    /// <param name="pic_width">帧的宽度</param>
    /// <param name="pic_height">帧的高度</param>
    /// <param name="pic_sar">帧的宽高比</param>
    void calculate_display_rect(SDL_Rect* rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar);
    /// <summary>
    /// 
    /// </summary>
    /// <param name="tex"></param>
    /// <param name="frame"></param>
    /// <param name="img_convert_ctx"></param>
    /// <returns></returns>
    int upload_texture(SDL_Texture* tex, AVFrame* frame, struct SwsContext** img_convert_ctx);
    void video_image_display(VideoState* is);
    void stream_component_close(VideoState* is, int stream_index);
    void stream_close(VideoState* is);
    /// <summary>
    /// 返回当前时钟的时间值，即当前视频（或音频）播放的时间戳
    /// </summary>
    /// <param name="c"></param>
    /// <returns></returns>
    double get_clock(Clock* c);
	/// <summary>
    /// 
    /// </summary>
    /// <param name="c">：is->的Clock</param>
    /// <param name="pts"></param>
    /// <param name="serial"></param>
    void set_clock(Clock* c, double pts, int serial);
    void set_clock_speed(Clock* c, double speed);
	/// <summary>
    /// 初始化时钟，设置速度为1.0，设置Clock的序列号，内部调用set_clock()
    /// </summary>
    /// <param name="c">：is->的Clock</param>
    /// <param name="queue_serial">：is->PacketQueue的serial，该值在初始化包队列的时候默认值为0</param>
    void init_clock(Clock* c, int* queue_serial);
    int get_master_sync_type(VideoState* is);
    double get_master_clock(VideoState* is);
    void check_external_clock_speed(VideoState* is);
    void stream_seek(VideoState* is, int64_t pos, int64_t rel);
	/// <summary>
	/// 临时取消暂停（或者说“解冻”播放器）
	/// </summary>
	/// <param name="is"></param>
    void stream_toggle_pause(VideoState* is);
    void toggle_pause(VideoState* is);
	/// <summary>
    /// 当播放暂停时，通常不会自动获取并显示新帧。但在执行 seek 后，用户希望看到定位到新位置的那一帧。
    /// </summary>
    /// <param name="is"></param>
    void step_to_next_frame(VideoState* is);
    /// <summary>
    /// 
    /// </summary>
    /// <param name="delay">上一帧的显示时间</param>
    /// <param name="is"></param>
    /// <returns>返回上一帧实际应该显示的时长</returns>
    double compute_target_delay(double delay, VideoState* is);
    /// <summary>
    /// 计算上一帧的显示持续时间
    /// </summary>
    /// <param name="is"></param>
    /// <param name="vp">rindex指示的帧</param>
    /// <param name="nextvp">rindex+rindex_shown指示的帧</param>
    /// <returns></returns>
    double vp_duration(VideoState* is, Frame* vp, Frame* nextvp);
    void update_video_pts(VideoState* is, double pts, int64_t pos, int serial);
public:
    void ffp_set_playback_rate(float rate);
    float ffp_get_playback_rate();
    /// <summary>
    /// 是否有变速请求
    /// </summary>
    /// <returns>true-有</returns>
    int ffp_get_playback_rate_change();
    /// <summary>
    /// 设置变速请求值
    /// </summary>
    /// <param name="change"></param>
    void ffp_set_playback_rate_change(int change);

    int64_t get_target_frequency();
    int     get_target_channels();
    int   is_normal_playback_rate();
private:
    static VideoCtl* m_pInstance; //< 单例指针

    bool m_bInited;	//< 初始化标志
    //Loop循环的标志
    //当用户选择终止或者播放一个视频时候会先设置为false以清除上一次的播放，然后才设置为true
    bool m_bPlayLoop; 

    //管家
    VideoState* m_CurStream;

    SDL_Window* window;
    SDL_Renderer* renderer;
    WId play_wid;//播放窗口

    /* options specified by the user */
    int screen_width;
    int screen_height;
    int startup_volume;

    //播放刷新循环线程
    std::thread m_tPlayLoopThread;

    int m_nFrameW;
    int m_nFrameH;

    float       pf_playback_rate;           // 播放速率
    int         pf_playback_rate_changed;   // 播放速率改变
public:
    // 变速器
    sonicStreamStruct* audio_speed_convert;
};


