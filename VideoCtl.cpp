﻿/*
 * @file 	videoctl.cpp
 * @date 	2018/01/21 12:15
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	视频控制类
 * @note
 */


#include <QDebug>
#include <QMutex>

#include <thread>
#include "videoctl.h"

#pragma execution_character_set("utf-8")

//extern关键字
extern QMutex g_show_rect_mutex;

static int framedrop = -1;
static int infinite_buffer = -1;
static int64_t audio_callback_time;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

int VideoCtl::realloc_texture(SDL_Texture** texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;   //access访问模式
    // 查询当前纹理的信息
    //texture：要查询的纹理对象。
	//format：指向Uint32的指针，用于接收纹理的像素格式。
	//access：指向int的指针，用于接收纹理的访问模式。
    //w：指向int的指针，用于接收纹理的宽度。
    //h：指向int的指针，用于接收纹理的高度。
    if (SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
        void* pixels;
        int pitch;
        // 销毁旧的纹理
        SDL_DestroyTexture(*texture);
        // 创建新的纹理
        //访问模式（此处为SDL_TEXTUREACCESS_STREAMING，表示纹理的数据可以被频繁更新）
        if (!(*texture = SDL_CreateTexture(renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        // 设置纹理的混合模式
        //混合模式定义了源像素（即要绘制的纹理像素）与目标像素（即已经存在于渲染目标上的像素）如何组合。​
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        // 如果需要，初始化纹理
        if (init_texture) {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
    }
    return 0;
}

void VideoCtl::calculate_display_rect(SDL_Rect* rect,
    int scr_xleft, int scr_ytop, int scr_width, int scr_height,
    int pic_width, int pic_height, AVRational pic_sar)
{
    //宽高比
    float aspect_ratio;
    int width, height, x, y;
    // 计算显示宽高比
    if (pic_sar.num == 0)
        aspect_ratio = 0;
    else
        aspect_ratio = av_q2d(pic_sar);

    if (aspect_ratio <= 0.0)
        aspect_ratio = 1.0;
    //将aspect_ratio乘以视频的宽高比，即(float)pic_width / (float)pic_height，得到实际的显示宽高比。
    aspect_ratio *= (float)pic_width / (float)pic_height;

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    //lrint 用于四舍五入， & ~1 的作用是确保宽度为偶数（通常视频编解码要求宽度为偶数）。
    width = lrint(height * aspect_ratio) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = lrint(width / aspect_ratio) & ~1;
    }
    //计算水平和垂直方向的偏移量：
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    //显示区域的左上角坐标，使其在屏幕上居中显示
    rect->x = scr_xleft + x;
    rect->y = scr_ytop + y;
    rect->w = FFMAX(width, 1);
    rect->h = FFMAX(height, 1);
}

int VideoCtl::upload_texture(SDL_Texture* tex, AVFrame* frame, struct SwsContext** img_convert_ctx) {
    int ret = 0;
    switch (frame->format) {
    case AV_PIX_FMT_YUV420P:
        //这里判断每个分量的行大小（linesize）是否为负值，因为负的行大小表示数据排列方式为倒序（比如图像垂直翻转），而 YUV 格式暂不支持这种情况。
        if (frame->linesize[0] < 0 || frame->linesize[1] < 0 || frame->linesize[2] < 0) {
            av_log(NULL, AV_LOG_ERROR, "Negative linesize is not supported for YUV.\n");
            return -1;
        }
        //用 SDL_UpdateYUVTexture 将 AVFrame 中的 Y、U、V 分量数据上传到纹理中。
        // 第二个参数是一个指向 SDL_Rect 的指针，用于指定更新纹理的区域。传入 NULL 意味着整个纹理都将被更新。
		//frame->data[0] 与 frame->linesize[0]
		//frame->data[0] 是一个指针，指向视频帧中 Y 分量（亮度）的数据。
		//frame->linesize[0] 表示 Y 分量每一行的字节数（pitch），用于告知 SDL 如何在内存中逐行读取数据。
        ret = SDL_UpdateYUVTexture(tex, NULL, frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]);
        break;
    case AV_PIX_FMT_BGRA:
        if (frame->linesize[0] < 0) {
            ret = SDL_UpdateTexture(tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
        }
        else {
            ret = SDL_UpdateTexture(tex, NULL, frame->data[0], frame->linesize[0]);
        }
        break;
    default:
        //当视频帧的格式不是 YUV420P 或 BGRA 时，需要将其转换为 BGRA 格式供 SDL 使用。
        /* This should only happen if we are not using avfilter... */
        *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
            frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height,
            AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
        if (*img_convert_ctx != NULL) {
            uint8_t* pixels[4];
            int pitch[4];
            if (!SDL_LockTexture(tex, NULL, (void**)pixels, pitch)) {
                sws_scale(*img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize,
                    0, frame->height, pixels, pitch);
                SDL_UnlockTexture(tex);
            }
        }
        else {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            ret = -1;
        }
        break;
    }
    return ret;
}

//显示视频画面
void VideoCtl::video_image_display(VideoState* is)
{
    Frame* vp;
    Frame* sp = NULL;
    SDL_Rect rect;
    vp = frame_queue_peek_last(&is->pictq);
    if (is->subtitle_st) {
        if (frame_queue_nb_remaining(&is->subpq) > 0) {
            sp = frame_queue_peek(&is->subpq);

            if (vp->pts >= sp->pts + ((float)sp->sub.start_display_time / 1000)) {
                if (!sp->uploaded) {
                    uint8_t* pixels[4];
                    int pitch[4];
                    int i;
                    if (!sp->width || !sp->height) {
                        sp->width = vp->width;
                        sp->height = vp->height;
                    }
                    if (realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
                        return;

                    for (i = 0; i < sp->sub.num_rects; i++) {
                        AVSubtitleRect* sub_rect = sp->sub.rects[i];

                        sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
                        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                        sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
                        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

                        is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                            0, NULL, NULL, NULL);
                        if (!is->sub_convert_ctx) {
                            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                            return;
                        }
                        if (!SDL_LockTexture(is->sub_texture, (SDL_Rect*)sub_rect, (void**)pixels, pitch)) {
                            sws_scale(is->sub_convert_ctx, (const uint8_t* const*)sub_rect->data, sub_rect->linesize,
                                0, sub_rect->h, pixels, pitch);
                            SDL_UnlockTexture(is->sub_texture);
                        }
                    }
                    sp->uploaded = 1;
                }
            }
            else
                sp = NULL;
        }
    }

    calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

    if (!vp->uploaded) {
        int sdl_pix_fmt = vp->frame->format == AV_PIX_FMT_YUV420P ? SDL_PIXELFORMAT_YV12 : SDL_PIXELFORMAT_ARGB8888;
        if (realloc_texture(&is->vid_texture, sdl_pix_fmt, vp->frame->width, vp->frame->height, SDL_BLENDMODE_NONE, 0) < 0)
            return;
        if (upload_texture(is->vid_texture, vp->frame, &is->img_convert_ctx) < 0)
            return;
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
        //通知宽高变化
        if (m_nFrameW != vp->frame->width || m_nFrameH != vp->frame->height)
        {
            m_nFrameW = vp->frame->width;
            m_nFrameH = vp->frame->height;
            emit SigFrameDimensionsChanged(m_nFrameW, m_nFrameH);
        }
    }
    //使用 SDL 的扩展渲染函数 SDL_RenderCopyEx 将上传后的视频纹理渲染到目标矩形 rect 上。
	//renderer：SDL 渲染器，用于实际绘制到窗口上。
	//is->vid_texture：视频纹理，存储了当前视频帧的数据。
	//NULL：表示整个纹理都被用于渲染（无源矩形裁剪）。
	//&rect：目标显示矩形，计算好后确保视频居中且保持正确比例。
	//0：旋转角度，这里不进行旋转。
	//NULL：旋转中心，默认使用中心点。
	//SDL_RendererFlip 标志：根据 vp->flip_v 判断是否需要垂直翻转。
    SDL_RenderCopyEx(renderer, is->vid_texture, NULL, &rect, 0, NULL, (SDL_RendererFlip)(vp->flip_v ? SDL_FLIP_VERTICAL : 0));
    //渲染字幕层（预留）
    if (sp) {
        SDL_RenderCopy(renderer, is->sub_texture, NULL, &rect);
    }
}


//关闭流对应的解码器等
void VideoCtl::stream_component_close(VideoState* is, int stream_index)
{
    AVFormatContext* ic = is->ic;
    AVCodecParameters* codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        decoder_abort(&is->auddec, &is->sampq);
        SDL_CloseAudio();
        decoder_destroy(&is->auddec);
        swr_free(&is->swr_ctx);
        av_freep(&is->audio_buf1);
        is->audio_buf1_size = 0;
        is->audio_buf = NULL;

        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        decoder_abort(&is->viddec, &is->pictq);
        decoder_destroy(&is->viddec);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        decoder_abort(&is->subdec, &is->subpq);
        decoder_destroy(&is->subdec);
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
        break;
    default:
        break;
    }
}
//关闭流
void VideoCtl::stream_close(VideoState* is)
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    is->read_tid.join();
	if (is->filename) {
		av_free(is->filename);
		is->filename = nullptr;
	}
    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(is, is->subtitle_stream);

    avformat_close_input(&is->ic);

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
    packet_queue_destroy(&is->subtitleq);

    /* free all pictures */
    frame_queue_destory(&is->pictq);
    frame_queue_destory(&is->sampq);
    frame_queue_destory(&is->subpq);
    SDL_DestroyCond(is->continue_read_thread);
    sws_freeContext(is->img_convert_ctx);
    sws_freeContext(is->sub_convert_ctx);
    av_free(is->filename);

    if (is->vid_texture)
        SDL_DestroyTexture(is->vid_texture);
    if (is->sub_texture)
        SDL_DestroyTexture(is->sub_texture);
    av_free(is);
}

double VideoCtl::get_clock(Clock* c)
{
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    }
    else {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

void VideoCtl::set_clock_at(Clock* c, double pts, int serial, double time)
{
    c->pts = pts;   /* 当前帧的pts */
    c->serial = serial;
    c->last_updated = time;  /* 最后更新的时间，实际上是当前的一个系统时间 */
    c->pts_drift = c->pts - time;   /* 当前帧pts和系统时间的差值，正常播放情况下两者的差值应该是比较固定的，因为两者都是以时间为基准进行线性增长 */
}

void VideoCtl::set_clock(Clock* c, double pts, int serial)
{
    //将微秒转化为秒
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

void VideoCtl::set_clock_speed(Clock* c, double speed)
{
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

void VideoCtl::init_clock(Clock* c, int* queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

void VideoCtl::sync_clock_to_slave(Clock* c, Clock* slave)
{
    double clock = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!std::isnan(slave_clock) && (std::isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

float VideoCtl::ffp_get_property_float(int id, float default_value)
{
    return 0;
}

void VideoCtl::ffp_set_property_float(int id, float value)
{
    switch (id) {
    case FFP_PROP_FLOAT_PLAYBACK_RATE:
        ffp_set_playback_rate(value);
        break;
        //            case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
        //                ffp_set_playback_volume(value);
        //                break;
    default:
        return;
    }
}

void VideoCtl::OnSpeed()
{
    pf_playback_rate += PLAYBACK_RATE_SCALE;    // 每次加刻度
    if (pf_playback_rate > PLAYBACK_RATE_MAX)
    {
        pf_playback_rate = PLAYBACK_RATE_MIN;
    }
    pf_playback_rate_changed = 1;
    emit SigSpeed(pf_playback_rate);
}

int VideoCtl::get_master_sync_type(VideoState* is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    }
    else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    }
    else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
double VideoCtl::get_master_clock(VideoState* is)
{
    double val;

    switch (get_master_sync_type(is)) {
    case AV_SYNC_VIDEO_MASTER:
        val = get_clock(&is->vidclk);
        break;
    case AV_SYNC_AUDIO_MASTER:
        val = get_clock(&is->audclk);
        break;
    default:
        val = get_clock(&is->extclk);
        break;
    }
    return val;
}

void VideoCtl::check_external_clock_speed(VideoState* is) {
    if (is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
        is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
        set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    }
    else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
        (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
        set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    }
    else {
        double speed = is->extclk.speed;
        if (speed != 1.0)
            set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}

/* seek in the stream */
void VideoCtl::stream_seek(VideoState* is, int64_t pos, int64_t rel)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
        SDL_CondSignal(is->continue_read_thread);
    }
}

void VideoCtl::stream_toggle_pause(VideoState* is)
{
    if (is->paused) {
		// 当处于暂停状态时，准备恢复播放：
		// 将当前时间与上次更新时间之差累加到 frame_timer 上，
		// 使得恢复播放时能够补偿暂停期间的时间间隔。
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
		// 如果 read_pause_return 没有返回 AVERROR(ENOSYS)（表示该接口有效），
	    // 则取消 video clock 的暂停标志。
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
		}
        // 更新 video clock，这里使用当前 video clock 的值重新设置时钟，
		// 使得 last_updated 与当前时间对齐，pts_drift 会根据新时间重新计算。
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    }
    // 无论暂停状态如何，都更新外部时钟 extclk，使其与当前时间同步
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    // 将 paused 标志取反，并同步设置音频（audclk）、视频（vidclk）和外部（extclk）时钟的暂停标志。
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

void VideoCtl::toggle_pause(VideoState* is)
{
    stream_toggle_pause(is);
    is->step = 0;
}

void VideoCtl::step_to_next_frame(VideoState* is)
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);
    //将 is->step 设置为 1，表示下一帧需要被读取并显示。
    //这样就能保证，即使播放器整体处于暂停状态，也能通过单步操作获得并显示一帧数据。
    is->step = 1;
}

double VideoCtl::compute_target_delay(double delay, VideoState* is)
{
    //合理的同步阈值
    double sync_threshold;
    //视频时钟与主时钟（可能是音频时钟）的差值
    double diff = 0;

    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {

		//首先计算出视频时钟与主时钟（可能是音频时钟）的差值
        diff = get_clock(&is->vidclk) - get_master_clock(is);
        
        //通过上述计算，sync_threshold 被限定在 40 毫秒到 100 毫秒之间。
        //分三种情况
        //delay > AV_SYNC_THRESHOLD_MAX = 0.1秒，则sync_threshold = 0.1秒
        //delay <AV_SYNC_THRESHOLD_MIN=0.04秒，则sync_threshold = 0.04秒
		//AV_SYNC_THRESHOLD_MIN = 0.0.4秒 <= delay <= AV_SYNC_THRESHOLD_MAX=0.1秒，则sync_threshold为delay本身
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        
        if (!std::isnan(diff) && fabs(diff) < is->max_frame_duration) {
            // 视频落后（视频时钟比主时钟慢）且偏差超过阈值（视频比音频快，且当前帧的显示时间足够长)
            if (diff <= -sync_threshold)
                //帧的显示时间被缩短，甚至可能立即切换到下一帧
                delay = FFMAX(0, delay + diff);

            //视频超前（视频时钟比主时钟快）且偏差超过阈值，同时当前延时大于帧复制阈值
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;

            //视频超前且偏差超过阈值，但当前延时较小（视频比音频快，但当前帧的显示时间较短)
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
        delay, -diff);

    return delay;
}

double VideoCtl::vp_duration(VideoState* is, Frame* vp, Frame* nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (std::isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration / pf_playback_rate;
        else
            return duration / pf_playback_rate;
    }
    else {
        return 0.0;
    }
}

void VideoCtl::update_video_pts(VideoState* is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    set_clock(&is->vidclk, pts / pf_playback_rate, serial);
    sync_clock_to_slave(&is->extclk, &is->vidclk);
}

void VideoCtl::ffp_set_playback_rate(float rate)
{
    pf_playback_rate = rate;
    pf_playback_rate_changed = 1;
}

float VideoCtl::ffp_get_playback_rate()
{
    return pf_playback_rate;
}

int VideoCtl::ffp_get_playback_rate_change()
{
    return pf_playback_rate_changed;
}

void VideoCtl::ffp_set_playback_rate_change(int change)
{
    pf_playback_rate_changed = change;
}

int64_t VideoCtl::get_target_frequency()
{
    if (m_CurStream)
    {
        return m_CurStream->audio_tgt.freq;
    }
    return 44100;       // 默认
}

int VideoCtl::get_target_channels()
{
    if (m_CurStream)
    {
        return m_CurStream->audio_tgt.channels;     //目前变速只支持1/2通道
    }
    return 2;       // 默认
}

int VideoCtl::is_normal_playback_rate()
{
    if (pf_playback_rate > 0.99 && pf_playback_rate < 1.01)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/* called to display each frame */
void VideoCtl::video_refresh(void* opaque, double* remaining_time)
{
    VideoState* is = (VideoState*)opaque;
    double time;

    Frame* sp, * sp2;

    double rdftspeed = 0.02;

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (is->video_st) {
    retry:
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
        }
        else {
            double last_duration, duration, delay;
            Frame* vp, * lastvp;

            /* dequeue the picture */
            lastvp = frame_queue_peek_last(&is->pictq);
            vp = frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused)
                goto display;

			/*如果视频播放过快，则重复播放上⼀帧，以等待⾳频；
			如果视频播放过慢，则丢帧追赶⾳频。实现的⽅式是，参考audio clock，计算上⼀帧（在屏幕上的那
			个画⾯）还应显示多久（含帧本身时⻓），然后与系统时刻对⽐，是否该显示下⼀帧了。*/
            /* compute nominal last_duration */

            last_duration = vp_duration(is, lastvp, vp);
            //            qDebug() << "last_duration ......" << last_duration;
            delay = compute_target_delay(last_duration, is);
            //            qDebug() << "delay ......" << delay;
            time = av_gettime_relative() / 1000000.0;

			//如果当前时间还未达到显示下一帧的时刻，则计算剩余等待时间（通过取当前剩余时间与预计差值的较小值更新）
            //然后跳转到显示部分，继续显示当前帧。
            if (time < is->frame_timer + delay) {
                //                 qDebug() << "(is->frame_timer + delay) - time " << is->frame_timer + delay - time;
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            }

            //当达到切换帧的时刻后，更新 is->frame_timer，使其累加延时（delay），准备下一帧显示的基准时间。
            is->frame_timer += delay;

            //如果延时 delay 大于零且系统时间与更新后的计时器之间的差距超过了一个允许的最大同步阈值（AV_SYNC_THRESHOLD_MAX）
            //说明内部计时器落后于系统时间太多，可能会导致长时间的滞后或累积误差
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            //1. 更新视频时钟
            SDL_LockMutex(is->pictq.mutex);
            if (!std::isnan(vp->pts))
                update_video_pts(is, vp->pts, vp->pos, vp->serial);
            SDL_UnlockMutex(is->pictq.mutex);
            //            qDebug() << "debug " << __LINE__;
            
            //2. 丢帧逻辑：判断是否需要丢弃当前帧
            //由于前面的frame_timer已更新要播放的一帧的时刻，因此这里要播放完这一帧是否还能追赶上当前时刻
            if (frame_queue_nb_remaining(&is->pictq) > 1) {
                Frame* nextvp = frame_queue_peek_next(&is->pictq);
                duration = vp_duration(is, vp, nextvp);
                if (!is->step && (framedrop > 0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) 
                    && time > is->frame_timer + duration) {
                    is->frame_drops_late++;
                    qDebug() << "frame_drops_late  " << is->frame_drops_late;
                    //切换到将下一要播放的帧并将其视为“已播放”
                    //“丢帧”
                    frame_queue_next(&is->pictq);
                    goto retry;
                }
            }
            //            qDebug() << "debug " << __LINE__;
            if (is->subtitle_st) {
                while (frame_queue_nb_remaining(&is->subpq) > 0) {
                    sp = frame_queue_peek(&is->subpq);

                    if (frame_queue_nb_remaining(&is->subpq) > 1)
                        sp2 = frame_queue_peek_next(&is->subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                        || (is->vidclk.pts > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
                        || (sp2 && is->vidclk.pts > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded) {
                            int i;
                            for (i = 0; i < sp->sub.num_rects; i++) {
                                AVSubtitleRect* sub_rect = sp->sub.rects[i];
                                uint8_t* pixels;
                                int pitch, j;

                                if (!SDL_LockTexture(is->sub_texture, (SDL_Rect*)sub_rect, (void**)&pixels, &pitch)) {
                                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                        memset(pixels, 0, sub_rect->w << 2);
                                    SDL_UnlockTexture(is->sub_texture);
                                }
                            }
                        }
                        frame_queue_next(&is->subpq);
                    }
                    else {
                        break;
                    }
                }
            }
            //切换到下一要播放的帧
            frame_queue_next(&is->pictq);
            is->force_refresh = 1;
            //            qDebug() << "debug " << __LINE__;
            // step为1，单步模式，当暂停时候seek，step就被设置为1，用以暂停显帧
            //在单步模式下，每次显示完一帧视频后，自动暂停播放，等待用户触发下一步操作（例如，按键事件）以继续播放下一帧。这样可以实现逐帧查看视频内容的功能。
            if (is->step && !is->paused)
                stream_toggle_pause(is);
        }
    display:
        /* display picture */
        if (is->force_refresh && is->pictq.rindex_shown)
            video_display(is);
    }
    is->force_refresh = 0;

    emit SigVideoPlaySeconds(get_master_clock(is) * pf_playback_rate);
}

int VideoCtl::queue_picture(VideoState* is, AVFrame* src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame* vp;

    if (!(vp = frame_queue_peek_writable(&is->pictq)))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    av_frame_move_ref(vp->frame, src_frame);
    frame_queue_push(&is->pictq);
    return 0;
}

//从视频队列中获取数据，并解码数据，得到可显示的视频帧
int VideoCtl::get_video_frame(VideoState* is, AVFrame* frame)
{
    int got_picture;

    //获取解码后的视频帧
    if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL)) < 0)
        return -1;

    if (got_picture) {

        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(is->video_st->time_base) * frame->pts;
		//根据输入上下文、视频流和当前帧信息来推测并设置帧的采样宽高比（SAR），便于后续正确显示图像。
        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);
        //如果所有条件满足，认为当前帧太“早”，需要丢弃。
        if (framedrop > 0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                if (!std::isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

//音频解码线程
int VideoCtl::audio_thread(void* arg)
{
    VideoState* is = (VideoState*)arg;
    AVFrame* frame = av_frame_alloc();
    Frame* af;

    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);
    do {
        if ((got_frame = decoder_decode_frame(&is->auddec, frame, NULL)) < 0)
            goto the_end;
        if (got_frame) {
            //每个采样点对应的时间单位是 1 / frame->sample_rate 秒。
            tb = { 1, frame->sample_rate };
            if (!(af = frame_queue_peek_writable(&is->sampq)))
                goto the_end;
            //该帧的显示时间（以秒为单位）
            af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            af->pos = av_frame_get_pkt_pos(frame);
            af->serial = is->auddec.pkt_serial;
            //当前帧的持续时间:采样数除以采样率
            af->duration = av_q2d({ frame->nb_samples, frame->sample_rate });
            av_frame_move_ref(af->frame, frame);
            frame_queue_push(&is->sampq);
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:
    av_frame_free(&frame);
    return ret;
}

//视频解码线程
int VideoCtl::video_thread(void* arg)
{
    VideoState* is = (VideoState*)arg;
    AVFrame* frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

    if (!frame)
    {
        return AVERROR(ENOMEM);
    }

    //循环从队列中获取视频帧
    for (;;) {
        ret = get_video_frame(is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;
        //一帧的显示时间
        duration = (frame_rate.num && frame_rate.den ? av_q2d(/*(AVRational) */{ frame_rate.den, frame_rate.num }) : 0);
        //当前帧的pts（以秒显示）
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        //将视频帧入队列
        ret = queue_picture(is, frame, pts, duration, av_frame_get_pkt_pos(frame), is->viddec.pkt_serial);
        av_frame_unref(frame);

        if (ret < 0)
            goto the_end;
    }
the_end:

    av_frame_free(&frame);
    return 0;
}

int VideoCtl::subtitle_thread(void* arg)
{
    VideoState* is = (VideoState*)arg;
    Frame* sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = frame_queue_peek_writable(&is->subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(&is->subdec, NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subdec.pkt_serial;
            sp->width = is->subdec.avctx->width;
            sp->height = is->subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_queue_push(&is->subpq);
        }
        else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

/* copy samples for viewing in editor window */
void VideoCtl::update_sample_display(VideoState* is, short* samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

int VideoCtl::synchronize_audio(VideoState* is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;
    //如果主同步时钟不是音频（例如是视频或外部时钟），那么音频就需要进行同步调整。只有在这种情况下，才会尝试调整采样数。
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!std::isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            }
            else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                    diff, avg_diff, wanted_nb_samples - nb_samples,
                    is->audio_clock, is->audio_diff_threshold);
            }
        }
        else {
            /* too big difference : may be initial PTS errors, so
            reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum = 0;
        }
    }
    return wanted_nb_samples;
}

int VideoCtl::audio_decode_frame(VideoState* is)
{
    int data_size, resampled_data_size; //用于保存原始数据大小和重采样后的数据大小。
    int64_t dec_channel_layout; //存储解码器实际使用的通道布局
    av_unused double audio_clock0;  //用于保存解码前的音频时钟值（av_unused 表示此变量在某些编译器配置下可能不被使用）。
	int wanted_nb_samples;  //经过同步调整后希望解码器输出的样本数量。
    Frame* af;
    if (is->paused)
        return -1;
    //从音频帧队列中获取一帧数据
    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep(1000);
        }
#endif
        if (!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);
    //end
    //计算当前音频帧中数据所占的字节数
    data_size = av_samples_get_buffer_size(NULL, av_frame_get_channels(af->frame),
        af->frame->nb_samples,
        (AVSampleFormat)af->frame->format, 1);
    //确定通道布局
    dec_channel_layout =
        (af->frame->channel_layout && av_frame_get_channels(af->frame) == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(av_frame_get_channels(af->frame));
    //根据音频同步需求调整采样数。
    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);
    //配置重采样（SWR）上下文
    //如果解码帧的格式和sdl支持的格式不一致
    if (af->frame->format != is->audio_src.fmt ||
        dec_channel_layout != is->audio_src.channel_layout ||
        af->frame->sample_rate != is->audio_src.freq ||
        (wanted_nb_samples != af->frame->nb_samples && !is->swr_ctx)) {
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(NULL,
            is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
            dec_channel_layout, (AVSampleFormat)af->frame->format, af->frame->sample_rate,
            0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), av_frame_get_channels(af->frame),
                is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels = av_frame_get_channels(af->frame);
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (AVSampleFormat)af->frame->format;
    }
    //重采样处理
    if (is->swr_ctx) {
        //原始音频数据，通常是一个数组，每个元素对应一个音频通道的数据指针。
        const uint8_t** in = (const uint8_t**)af->frame->extended_data;
        //输出缓冲区指针 is->audio_buf1。
        uint8_t** out = &is->audio_buf1;
        //按比例转换为目标采样率下的样本数，并额外留出 256 个样本的余量。
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        //计算输出缓冲区大小
        int out_size = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        //如果实际样本数和期望样本数不同，则调用 swr_set_compensation 调整重采样上下文，补偿采样数差异。
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        //回先清空原来的内存再次申请
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        //调用重采样函数
        //返回成功转换后的样本数
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            //做个安全检查
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    }
    else {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }
    //更新音频时钟
    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!std::isnan(af->pts))
        //audio_clock 的计算仍然基于原始帧的信息，而不是重采样后的数据。
        is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
    return resampled_data_size;
}

/// <summary>
/// SDL回调获取数据的函数
/// </summary>
/// <param name="opaque">：VideoState*</param>
/// <param name="stream">SDL数据流</param>
/// <param name="len">SDL数据流需要的大小</param>
void sdl_audio_callback(void* opaque, Uint8* stream, int len)
{
    VideoState* is = (VideoState*)opaque;
    int audio_size, len1;
    //单例模式
    VideoCtl* pVideoCtl = VideoCtl::GetInstance();
    //提前获取以便于后续调整音频时钟（例如补偿音频硬件缓冲延迟），使得更新后的时钟能更贴近实际播放时刻。
    audio_callback_time = av_gettime_relative();
    while (len > 0) {
        // 从帧队列读取缓存
        if (is->audio_buf_index >= is->audio_buf_size) {        // 数据已经处理完毕了，需要读取新的
            audio_size = pVideoCtl->audio_decode_frame(is);
            //            qDebug() << "1 audio_buf_size: " << audio_size;
            if (audio_size < 0) {
                /* if error, just output silence */
                is->audio_buf = NULL;
                is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
            }
            else {
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
            // 2 是否需要做变速
            if (pVideoCtl->ffp_get_playback_rate_change())
            {
                pVideoCtl->ffp_set_playback_rate_change(0);
                // 初始化
                if (pVideoCtl->audio_speed_convert)
                {
                    // 先释放
                    sonicDestroyStream(pVideoCtl->audio_speed_convert);
                }
                // 再创建
                pVideoCtl->audio_speed_convert = sonicCreateStream(pVideoCtl->get_target_frequency(),
                    pVideoCtl->get_target_channels());
                // 设置变速系数
                sonicSetSpeed(pVideoCtl->audio_speed_convert, pVideoCtl->ffp_get_playback_rate());
                //确保音频在变速后，声音的音调依然保持原样，不会因为速度变化而失真。
                //保持音高不变
                sonicSetPitch(pVideoCtl->audio_speed_convert, 1.0);
                //保持节奏不变：
                sonicSetRate(pVideoCtl->audio_speed_convert, 1.0);
            }
			// 不是正常播放则需要修改
			// 需要修改  is->audio_buf_index is->audio_buf_size is->audio_buf
            if (!pVideoCtl->is_normal_playback_rate() && is->audio_buf)
            {
                // 当前音频缓冲区（is->audio_buf_size，以字节为单位）的大小转换为样本数。
				//is->audio_tgt.channels 表示目标音频通道数。
                //av_get_bytes_per_sample(is->audio_tgt.fmt) 返回目标采样格式中每个样本占用的字节数
                int actual_out_samples = is->audio_buf_size /
                    (is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt));
                // 计算处理后的点数
                int out_ret = 0;
                int out_size = 0;
                int num_samples = 0;
                int sonic_samples = 0;
                //根据目标采样格式判断数据类型，调用 Sonic 库的相应接口
                if (is->audio_tgt.fmt == AV_SAMPLE_FMT_FLT)
                {
                    out_ret = sonicWriteFloatToStream(pVideoCtl->audio_speed_convert,
                        (float*)is->audio_buf,
                        actual_out_samples);
                }
                else  if (is->audio_tgt.fmt == AV_SAMPLE_FMT_S16)
                {
                    out_ret = sonicWriteShortToStream(pVideoCtl->audio_speed_convert,
                        (short*)is->audio_buf,
                        actual_out_samples);
                }
                else
                {
                    av_log(NULL, AV_LOG_ERROR, "sonic unspport ......\n");
                }
                num_samples = sonicSamplesAvailable(pVideoCtl->audio_speed_convert);
                // 2通道  目前只支持2通道；得到输出缓冲区所需的总字节数。
                out_size = (num_samples)*av_get_bytes_per_sample(is->audio_tgt.fmt) * is->audio_tgt.channels;
                av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
                if (out_ret)
                {
                    // 从流中读取处理好的数据
                    if (is->audio_tgt.fmt == AV_SAMPLE_FMT_FLT) {
                        sonic_samples = sonicReadFloatFromStream(pVideoCtl->audio_speed_convert,
                            (float*)is->audio_buf1,
                            num_samples);
                    }
                    else  if (is->audio_tgt.fmt == AV_SAMPLE_FMT_S16)
                    {
                        sonic_samples = sonicReadShortFromStream(pVideoCtl->audio_speed_convert,
                            (short*)is->audio_buf1,
                            num_samples);
                    }
                    else
                    {
                        qDebug() << "sonic unspport ......";
                    }
                    is->audio_buf = is->audio_buf1;
                    //                    qDebug() << "mdy num_samples: " << num_samples;
                    //                    qDebug() << "orig audio_buf_size: " << is->audio_buf_size;
                    //重新计算 is->audio_buf_size 为 sonic_samples 乘以通道数和每个采样的字节数，反映重采样后数据的总大小。
                    is->audio_buf_size = sonic_samples * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
                    //                    qDebug() << "mdy audio_buf_size: " << is->audio_buf_size;
                    is->audio_buf_index = 0;
                }
            }
        }
        if (is->audio_buf_size == 0)
            continue;
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        if (is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (is->audio_buf)
                SDL_MixAudio(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, len1, is->audio_volume);
        }
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!std::isnan(is->audio_clock)) {
        double audio_clock = is->audio_clock / pVideoCtl->ffp_get_playback_rate();
        //为什么不直接使用audio_decode_frame中的af->pts 
        //因为这个值代表了解码帧的时间戳，但它没有反映数据从解码到实际输出之间的延迟。
        //实际上，解码后的音频数据需要先进入硬件缓冲区，再经过一段延迟后才会被播放。
        //为了准确同步音视频，时钟更新时需要考虑这一部分缓冲延迟（由 audio_hw_buf_size 和 audio_write_buf_size 表示）。
        //audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec
        //对 is->audio_clock 进行了补偿，从而得到更接近实际播放时刻的时间戳。同时，audio_callback_time 作为当前回调的时间记录，也被传入以更新时钟的最后更新时间和漂移值。
        //虽然在一开始获取 audio_callback_time 时没有考虑延迟，但在 set_clock_at 中通过减去由缓冲区大小和未写入数据计算出的延迟，确保最终设置的 pts 能反映实际播放的音频时间，进而计算出准确的 pts_drift。
        pVideoCtl->set_clock_at(&is->audclk, 
            audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, 
            is->audio_clock_serial, audio_callback_time / 1000000.0);
        pVideoCtl->sync_clock_to_slave(&is->extclk, &is->audclk);
    }
}

int VideoCtl::audio_open(void* opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate,
    struct AudioParams* audio_hw_params)
{
	//typedef struct SDL_AudioSpec
	//{
	//	int freq;                   // 采样率
	//	SDL_AudioFormat format;      // 采样格式 (如 AUDIO_S16SYS, AUDIO_F32SYS)
	//	Uint8 channels;              // 通道数
	//	Uint8 silence;               // 静音时填充值
	//	Uint16 samples;              // 音频缓冲区大小
	//	Uint16 padding;              // 填充（未使用）
	//	Uint32 size;                 // 缓冲区总大小（字节）
	//	SDL_AudioCallback callback;  // 回调函数
	//	void* userdata;              // 用户数据
	//} SDL_AudioSpec;

    SDL_AudioSpec wanted_spec, spec;
    const char* env;
    //提供备选的通道数和采样率列表
    static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
    static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
    //end
    
    //设置为 next_sample_rates 数组最后一个元素的索引，通常用于选择一个较高的采样率作为备用。
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

	//获取环境变量 SDL_AUDIO_CHANNELS 的值。如果设置了这个环境变量，用户期望的通道数就由这个变量指定。
    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        //将环境变量字符串转换为整数，得到期望的通道数
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }

    //AVCodecContext 的 channel_layout 参数有时候是“不完整”或者“不一致”的。
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        //移除立体声降混（stereo downmix）的标志
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }

    //从 192000 开始向前查找，直到找到一个 比 wanted_spec.freq 小的采样率，作为 fallback 的开始点。
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    //SDL的参数
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    //wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC--计算每个回调周期目标处理的样本数量。
    //wanted_spec.freq 是采样率（例如 44100 Hz）
    //SDL_AUDIO_MAX_CALLBACKS_PER_SEC 表示每秒最大回调次数
	//av_log2(...)计算前面那个数值的以 2 为底的对数。通常返回的是一个整数（floor(log2(x))），表示接近这个数值的二的幂次。
	//2 << av_log2(...)等价于 2 * (1 << av_log2(...))，也就是 2 乘以 2 的某个幂次。这样可以得到一个基于目标样本数的、较为“对齐”的缓冲区大小。
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;

    //尽力按照你给的 wanted_spec 打开设备，但实际成功后会把设备最终使用的真实参数填入 spec！
    //尝试用预期的参数去打开sdl设备；如若失败，则会进入到while循环自动寻找合适的参数
    while (SDL_OpenAudio(&wanted_spec, &spec) < 0) {

        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
            wanted_spec.channels, wanted_spec.freq, SDL_GetError());

        //从 next_nb_channels 数组中取一个备选通道数，用来替换当前的 wanted_spec.channels
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];

        //如果 next_nb_channels[...] 返回的是 0（意味着当前通道数没有备选项），那么就要尝试 换采样率：
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            //将通道数还原为最初用户传入的值（重新开始尝试该通道数在新采样率下能否成功打开）
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                    "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        //重新根据当前尝试的 channels 获取合适的声道布局
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }

    //实际打开的通道数和想要的通道数不一致
    //因为后续还需要wanted_channel_layout，而spec的结构体没有通道布局变量
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    //将实际sdl支持的播放格式映射到ffmpeg对应格式上
    switch (spec.format)
    {
    case AUDIO_U8:
        audio_hw_params->fmt = AV_SAMPLE_FMT_U8;
        break;
    case AUDIO_S16LSB:
    case AUDIO_S16MSB:
        audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
        break;
    case AUDIO_S32LSB:
    case AUDIO_S32MSB:
        audio_hw_params->fmt = AV_SAMPLE_FMT_S32;
        break;
    case AUDIO_F32LSB:
    case AUDIO_F32MSB:
        audio_hw_params->fmt = AV_SAMPLE_FMT_FLT;
        break;
    default:
        audio_hw_params->fmt = AV_SAMPLE_FMT_U8;
        break;
    }

    //audio_hw_params->fmt = AV_SAMPLE_FMT_FLT;
    //    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels = spec.channels;

    //一帧（1 个样本）的多声道音频数据大小。
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    //计算一秒的大小
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    return spec.size;
}

int VideoCtl::stream_component_open(VideoState* is, int stream_index)
{
    AVFormatContext* ic = is->ic;
    AVCodecContext* avctx;
    AVCodec* codec;
    const char* forced_codec_name = NULL;
    AVDictionary* opts = NULL;
    AVDictionaryEntry* t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = 0;
    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    //初始化结构体
    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    av_codec_set_pkt_timebase(avctx, ic->streams[stream_index]->time_base);
    //寻找解码器
    codec = avcodec_find_decoder(avctx->codec_id);
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO: is->last_audio_stream = stream_index; break;
    case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; break;
    case AVMEDIA_TYPE_VIDEO: is->last_video_stream = stream_index; break;
    }
    avctx->codec_id = codec->id;
    //设置分辨率级别
    if (stream_lowres > av_codec_get_max_lowres(codec)) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
            av_codec_get_max_lowres(codec));
        stream_lowres = av_codec_get_max_lowres(codec);
    }
    av_codec_set_lowres(avctx, stream_lowres);
    //end
#if FF_API_EMU_EDGE
    if (stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif
#if FF_API_EMU_EDGE
    if (codec->capabilities & AV_CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif
    opts = nullptr;//filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    //让解码器自动选择线程数
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    //告诉解码器使用低分辨率模式（通常用于减小解码负担或适应设备性能）
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
	//解码器输出的帧会带有引用计数，这有助于管理内存和避免数据复制，尤其在多线程和复杂处理流程中更安全。
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    //打开解码器
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    //经过之前的设置，预期所有提供的选项都应该被解码器所识别并从字典中移除。
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }
    is->eof = 0;
    //AVDISCARD_DEFAULT 通常表示保留需要参考的帧，而丢弃一些可丢弃的帧。
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        sample_rate = avctx->sample_rate;
        nb_channels = avctx->channels;
        channel_layout = avctx->channel_layout;
        /* prepare audio output */
        //打开音频流
        if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        is->audio_hw_buf_size = ret;
        //初始化先设置audio_src等于audio_tgt
        is->audio_src = is->audio_tgt;
        is->audio_buf_size = 0;
        is->audio_buf_index = 0;
        //初始化音频同步平均滤波器
        is->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
        we correct audio sync only if larger than this threshold */
        //设置音频同步校正阈值
        is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;
        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];
        decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
        //针对特殊格式设置起始PTS
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        packet_queue_start(is->auddec.queue);
        //创建音频解码线程，开始音频解码
        is->auddec.decode_thread = std::thread(&VideoCtl::audio_thread, this, is);
        SDL_PauseAudio(0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];
        decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
        packet_queue_start(is->viddec.queue);
        //创建视频解码线程，开始视频解码
        is->viddec.decode_thread = std::thread(&VideoCtl::video_thread, this, is);
        is->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];
        //创建字幕解码线程，开始字幕解码
        decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread);
        packet_queue_start(is->subdec.queue);
        is->subdec.decode_thread = std::thread(&VideoCtl::subtitle_thread, this, is);
        break;
    default:
        break;
    }
    goto out;
fail:
    avcodec_free_context(&avctx);
out:
    av_dict_free(&opts);
    return ret;
}
/// <summary>
/// AVFormatContext的中断回调函数
/// </summary>
/// <param name="ctx"></param>
/// <returns></returns>
int decode_interrupt_cb(void* ctx)
{
    VideoState* is = (VideoState*)ctx;
    return is->abort_request;
}

int VideoCtl::stream_has_enough_packets(AVStream* st, int stream_id, PacketQueue* queue) {
    return stream_id < 0 ||
        queue->abort_request ||
        (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

int VideoCtl::is_realtime(AVFormatContext* s)
{
    if (!strcmp(s->iformat->name, "rtp")
        || !strcmp(s->iformat->name, "rtsp")
        || !strcmp(s->iformat->name, "sdp")
        )
        return 1;

    if (s->pb && (!strncmp(s->filename, "rtp:", 4)
        || !strncmp(s->filename, "udp:", 4)
        )
        )
        return 1;
    return 0;
}

void VideoCtl::ReadThread(VideoState* is)
{
    //VideoState *is = (VideoState *)arg;
    AVFormatContext* ic = NULL;
    int err, i, ret;
    //流序号数组
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket pkt1, * pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;
    AVDictionaryEntry* t;
    AVDictionary** opts;
    int orig_nb_streams;
    SDL_mutex* wait_mutex = SDL_CreateMutex();
    int scan_all_pmts_set = 0;
    int64_t pkt_ts;
    //ffplay中用户命令行输入的
    const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = { 0 };
    if (!wait_mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    memset(st_index, -1, sizeof(st_index));
    is->last_video_stream = is->video_stream = -1;
    is->last_audio_stream = is->audio_stream = -1;
    is->last_subtitle_stream = is->subtitle_stream = -1;
    is->eof = 0;
    //构建 处理封装格式 结构体
    ic = avformat_alloc_context();
    if (!ic) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    //设置中断回调函数
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;
    //打开文件，获得封装等信息
    err = avformat_open_input(&ic, is->filename, is->iformat, nullptr/*&format_opts*/);
    if (err < 0) {
        //print_error(is->filename, err);
        ret = -1;
        goto fail;
    }
    is->ic = ic;
    //每个流的下一个数据包中注入全局侧数据，并在随后的任何 seek 操作后继续注入这些数据。
    av_format_inject_global_side_data(ic);
    opts = nullptr;// setup_find_stream_info_opts(ic, codec_opts);
    orig_nb_streams = ic->nb_streams;
    //读取一部分视音频数据并且获得一些相关的信息
    err = avformat_find_stream_info(ic, opts);
    //     for (i = 0; i < orig_nb_streams; i++)
    //         av_dict_free(&opts[i]);
    //     av_freep(&opts);
    if (err < 0) {
        av_log(NULL, AV_LOG_WARNING,
            "%s: could not find codec parameters\n", is->filename);
        ret = -1;
        goto fail;
    }
    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    is->realtime = is_realtime(ic);
    emit SigVideoTotalSeconds(ic->duration / 1000000LL);
    //ffplay命令行：查找用户命令行制定的流是否有效
    for (i = 0; i < ic->nb_streams; i++) {
        AVStream* st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wanted_stream_spec[(AVMediaType)i] && st_index[(AVMediaType)i] == -1) {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[(AVMediaType)i], av_get_media_type_string((AVMediaType)i));
            st_index[(AVMediaType)i] = -1;
        }
    }
    //如果用户指定的流无效
    //获得视频、音频、字幕的流索引
    st_index[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
            st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    st_index[AVMEDIA_TYPE_AUDIO] =
        av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
            st_index[AVMEDIA_TYPE_AUDIO],
            st_index[AVMEDIA_TYPE_VIDEO],
            NULL, 0);
    st_index[AVMEDIA_TYPE_SUBTITLE] =
        av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
            st_index[AVMEDIA_TYPE_SUBTITLE],
            (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                st_index[AVMEDIA_TYPE_AUDIO] :
                st_index[AVMEDIA_TYPE_VIDEO]),
            NULL, 0);
    //拟初步获得视频的一些数据
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream* st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters* codecpar = st->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
    }
    /* open the streams */
    //打开音频流
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }
    //打开视频流
    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }
    //打开字幕流
    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }
    if (is->video_stream < 0 && is->audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
            is->filename);
        ret = -1;
        goto fail;
    }
    //只在实时流的时候有效
    if (infinite_buffer < 0 && is->realtime)
        infinite_buffer = 1;
    //读取视频数据
    for (;;) {
        if (is->abort_request)
            break;
        if (is->paused != is->last_paused) {
            is->last_paused = is->paused;
            if (is->paused)
                is->read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }
        //如果有seek请求
        if (is->seek_req) {
            int64_t seek_target = is->seek_pos;
            int64_t seek_min = is->seek_rel > 0 ? seek_target - is->seek_rel + 2 : INT64_MIN;
            int64_t seek_max = is->seek_rel < 0 ? seek_target - is->seek_rel - 2 : INT64_MAX;
            // FIXME the +-2 is due to rounding being not done in the correct direction in generation
            //      of the seek_pos/seek_rel variables
            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                    "%s: error while seeking\n", is->ic->filename);
            }
            else {
                if (is->audio_stream >= 0) {
                    packet_queue_flush(&is->audioq);
                    packet_queue_put(&is->audioq, &flush_pkt);
                }
                if (is->subtitle_stream >= 0) {
                    packet_queue_flush(&is->subtitleq);
                    packet_queue_put(&is->subtitleq, &flush_pkt);
                }
                if (is->video_stream >= 0) {
                    packet_queue_flush(&is->videoq);
                    packet_queue_put(&is->videoq, &flush_pkt);
                }
                if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                    set_clock(&is->extclk, NAN, 0);
                }
                else {
                    set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            is->seek_req = 0;
            is->queue_attachments_req = 1;
            is->eof = 0;
            //seek（定位）操作后如果处于暂停状态，通常希望立即显示定位后的新画面，而不会一直停留在上一个画面上。
            if (is->paused)
                step_to_next_frame(is);
        }
        //如果是一些MP3的专辑封面之类的
        if (is->queue_attachments_req) {
            if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy;
                if ((ret = av_copy_packet(&copy, &is->video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->videoq, &copy);
                packet_queue_put_nullpacket(&is->videoq, is->video_stream);
            }
            is->queue_attachments_req = 0;
        }
        /* if the queue are full, no need to read more */
        if (infinite_buffer < 1 &&
            (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
                || (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
                    stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
                    stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
            /* wait 10 ms */
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        if (!is->paused &&
            (!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0))) {

            //播放结束
            emit SigStop();
            continue;
        }
        //按帧读取
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
                is->eof = 1;
            }
            //检查是否有 I/O 错误
            if (ic->pb && ic->pb->error)
                break;
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        else {
            is->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = AV_NOPTS_VALUE == AV_NOPTS_VALUE ||
            (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
            av_q2d(ic->streams[pkt->stream_index]->time_base) -
            (double)(0) / 1000000
            <= ((double)AV_NOPTS_VALUE / 1000000);
        //按数据帧的类型存放至对应队列
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            packet_queue_put(&is->audioq, pkt);
        }
        else if (pkt->stream_index == is->video_stream && pkt_in_play_range
            && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            packet_queue_put(&is->videoq, pkt);
        }
        else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            packet_queue_put(&is->subtitleq, pkt);
        }
        else {
            av_packet_unref(pkt);
        }
    }
    ret = 0;
fail:
    if (ic && !is->ic)
        avformat_close_input(&ic);
    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    SDL_DestroyMutex(wait_mutex);
    return;
}

VideoState* VideoCtl::stream_open(const char* filename)
{
    VideoState* is;
    //构造视频状态类
    is = (VideoState*)av_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    //视频文件名，实际上是分配了空间
    is->filename = av_strdup(filename);
    if (!is->filename)
        goto fail;
    //指定输入格式
    is->ytop = 0;
    is->xleft = 0;
    /* start video display */
    //初始化视频帧队列
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    //初始化字幕帧队列
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    //初始化音频帧队列
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;
    //初始化队列中的数据包
    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0)
        goto fail;
    //构建控制继续读取线程的信号量
    if (!(is->continue_read_thread = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }
    //视频、音频 时钟
    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    //音量
    if (startup_volume < 0)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", startup_volume);
    if (startup_volume > 100)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", startup_volume);
    //将startup_volumex限定在0到100之间
    startup_volume = av_clip(startup_volume, 0, 100);
    //SDL_MIX_MAXVOLUME * startup_volume / 100,转化为SDL的音量比例
    startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
    is->audio_volume = startup_volume;
    //转换后的音量值归一化到 0 到 1 的区间
    emit SigVideoVolume(startup_volume * 1.0 / SDL_MIX_MAXVOLUME);
    emit SigPauseStat(is->paused);
    //以音频为基准
    is->av_sync_type = AV_SYNC_AUDIO_MASTER;
    //构建读取线程（重要）
    is->read_tid = std::thread(&VideoCtl::ReadThread, this, is);
    return is;
fail:
    stream_close(is);
    return NULL;
}

void VideoCtl::stream_cycle_channel(VideoState* is, int codec_type)
{
    AVFormatContext* ic = is->ic;
    int start_index, stream_index;
    int old_index;
    AVStream* st;
    AVProgram* p = NULL;
    int nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index = is->video_stream;
    }
    else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index = is->audio_stream;
    }
    else {
        start_index = is->last_subtitle_stream;
        old_index = is->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codecpar->sample_rate != 0 &&
                    st->codecpar->channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
        av_get_media_type_string((AVMediaType)codec_type),
        old_index,
        stream_index);

    stream_component_close(is, old_index);
    stream_component_open(is, stream_index);
}


void VideoCtl::refresh_loop_wait_event(VideoState* is, SDL_Event* event) {
    double remaining_time = 0.0;
    //调用 SDL_PumpEvents() 函数从输入设备收集所有待处理的输入信息，并将其放入事件队列中。
    SDL_PumpEvents();
    //使用 SDL_PeepEvents() 函数从事件队列中获取事件,没有事件返回0
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) && m_bPlayLoop)
    {
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (!is->paused || is->force_refresh)
            video_refresh(is, &remaining_time);
        SDL_PumpEvents();
    }
}

void VideoCtl::seek_chapter(VideoState* is, int incr)
{
    int64_t pos = get_master_clock(is) * AV_TIME_BASE;
    int i;

    if (!is->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < is->ic->nb_chapters; i++) {
        AVChapter* ch = is->ic->chapters[i];
        if (av_compare_ts(pos, /*AV_TIME_BASE_Q*/{ 1, AV_TIME_BASE }, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= is->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
        /*AV_TIME_BASE_Q*/{ 1, AV_TIME_BASE }), 0);
}

void VideoCtl::LoopThread(VideoState* cur_stream)
{
    SDL_Event event;
    double incr, pos, frac;

    m_bPlayLoop = true;

    while (m_bPlayLoop)
    {
        double x;
        refresh_loop_wait_event(cur_stream, &event);
        switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_s: // S: Step to next frame
                step_to_next_frame(cur_stream);
                break;
            case SDLK_a:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                break;
            case SDLK_v:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                break;
            case SDLK_c:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_t:
                stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            default:
                break;
            }
            break;
        case SDL_WINDOWEVENT:
            //窗口大小改变事件
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
                screen_width = cur_stream->width = event.window.data1;
                screen_height = cur_stream->height = event.window.data2;
            case SDL_WINDOWEVENT_EXPOSED:
                cur_stream->force_refresh = 1;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            do_exit(cur_stream);
            break;
        default:
            break;
        }
    }
    do_exit(m_CurStream);
    //m_CurStream = nullptr;
}

int lockmgr(void** mtx, enum AVLockOp op)
{
    switch (op) {
    case AV_LOCK_CREATE:
        *mtx = SDL_CreateMutex();
        if (!*mtx) {
            av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
            return 1;
        }
        return 0;
    case AV_LOCK_OBTAIN:
        return !!SDL_LockMutex((SDL_mutex*)*mtx);
    case AV_LOCK_RELEASE:
        return !!SDL_UnlockMutex((SDL_mutex*)*mtx);
    case AV_LOCK_DESTROY:
        SDL_DestroyMutex((SDL_mutex*)*mtx);
        return 0;
    }
    return 1;
}


void VideoCtl::OnPlaySeek(double dPercent)
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    int64_t ts = dPercent * m_CurStream->ic->duration;
    if (m_CurStream->ic->start_time != AV_NOPTS_VALUE)
        ts += m_CurStream->ic->start_time;
    stream_seek(m_CurStream, ts, 0);
}

void VideoCtl::OnPlayVolume(double dPercent)
{
    startup_volume = dPercent * SDL_MIX_MAXVOLUME;
    if (m_CurStream == nullptr)
    {
        return;
    }
    m_CurStream->audio_volume = startup_volume;
}

void VideoCtl::OnSeekForward()
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    double incr = 5.0;
    double pos = get_master_clock(m_CurStream);
    if (std::isnan(pos))
        pos = (double)m_CurStream->seek_pos / AV_TIME_BASE;
    pos += incr;
    if (m_CurStream->ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->ic->start_time / (double)AV_TIME_BASE)
        pos = m_CurStream->ic->start_time / (double)AV_TIME_BASE;
    stream_seek(m_CurStream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
}

void VideoCtl::OnSeekBack()
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    double incr = -5.0;
    double pos = get_master_clock(m_CurStream);
    if (std::isnan(pos))
        pos = (double)m_CurStream->seek_pos / AV_TIME_BASE;
    pos += incr;
    if (m_CurStream->ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->ic->start_time / (double)AV_TIME_BASE)
        pos = m_CurStream->ic->start_time / (double)AV_TIME_BASE;
    stream_seek(m_CurStream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
}

void VideoCtl::UpdateVolume(int sign, double step)
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    double volume_level = m_CurStream->audio_volume ? (20 * log(m_CurStream->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    m_CurStream->audio_volume = av_clip(m_CurStream->audio_volume == new_volume ? (m_CurStream->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);

    emit SigVideoVolume(m_CurStream->audio_volume * 1.0 / SDL_MIX_MAXVOLUME);
}

/* display the current picture, if any */
void VideoCtl::video_display(VideoState* is)
{
    if (!window)
        video_open(is);
    if (renderer)
    {
        //恰好显示控件大小在变化，则不刷新显示
        if (g_show_rect_mutex.tryLock())
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // 设置渲染颜色为黑色
            SDL_RenderClear(renderer);  // 清除渲染器内容
            video_image_display(is);    // 将视频帧渲染到当前渲染目标上
            SDL_RenderPresent(renderer);    //将渲染器的内容呈现到关联的窗口上，更新显示
            g_show_rect_mutex.unlock();
        }
    }

}

int VideoCtl::video_open(VideoState* is)
{
    int w, h;

    w = screen_width;
    h = screen_height;
    if (!window) {
        int flags = SDL_WINDOW_SHOWN;
        flags |= SDL_WINDOW_RESIZABLE;

        window = SDL_CreateWindowFrom((void*)play_wid);
        SDL_GetWindowSize(window, &w, &h);//初始宽高设置为显示控件宽高，因此会覆盖原来的w和h
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (window) {
            SDL_RendererInfo info;
            if (!renderer)
                //尝试先使用硬件加速方式创建渲染器（指定 SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC）
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer) {
                av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
                //上述硬件渲染如果失败则回退使用软件渲染。
                renderer = SDL_CreateRenderer(window, -1, 0);
            }
            if (renderer) {
                if (!SDL_GetRendererInfo(renderer, &info))
                    av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", info.name);
            }
        }
    }
    else {
        //改变显示窗口宽高
        SDL_SetWindowSize(window, w, h);
    }

    if (!window || !renderer) {
        av_log(NULL, AV_LOG_FATAL, "SDL: could not set video mode - exiting\n");
        do_exit(is);
    }

    is->width = w;
    is->height = h;

    return 0;
}

void VideoCtl::do_exit(VideoState*& is)
{
    if (is)
    {
        stream_close(is);
        is = nullptr;
    }
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    emit SigStopFinished();
}

void VideoCtl::OnAddVolume()
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    UpdateVolume(1, SDL_VOLUME_STEP);
}

void VideoCtl::OnSubVolume()
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    UpdateVolume(-1, SDL_VOLUME_STEP);
}

void VideoCtl::OnPause()
{
    if (m_CurStream == nullptr)
    {
        return;
    }
    toggle_pause(m_CurStream);
    emit SigPauseStat(m_CurStream->paused);
}

void VideoCtl::OnStop()
{
    m_bPlayLoop = false;
}

VideoCtl::VideoCtl(QObject* parent) :
    QObject(parent),
    m_bInited(false),
    m_CurStream(nullptr),
    m_bPlayLoop(false),
    screen_width(0),
    screen_height(0),
    startup_volume(30),
    renderer(nullptr),
    window(nullptr),
    m_nFrameW(0),
    m_nFrameH(0),
    pf_playback_rate(1.0),
    pf_playback_rate_changed(0),
    audio_speed_convert(NULL)
{
    //注册所有复用器、编码器
    av_register_all();
    //网络格式初始化
    avformat_network_init();
}

bool VideoCtl::Init()
{
    if (m_bInited == true)
    {
        return true;
    }

    if (ConnectSignalSlots() == false)
    {
        return false;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        return false;
    }
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    //注册自定义锁
    if (av_lockmgr_register(lockmgr))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize lock manager!\n");
        return false;
    }

    m_bInited = true;

    return true;
}

bool VideoCtl::ConnectSignalSlots()
{
    connect(this, &VideoCtl::SigStop, &VideoCtl::OnStop);
    return true;
}

VideoCtl* VideoCtl::m_pInstance = new VideoCtl();

VideoCtl* VideoCtl::GetInstance()
{
    if (false == m_pInstance->Init())
    {
        return nullptr;
    }
    return m_pInstance;
}

VideoCtl::~VideoCtl()
{
    if (m_tPlayLoopThread.joinable()) {
        m_tPlayLoopThread.join();
    }

    do_exit(m_CurStream);

    av_lockmgr_register(NULL);

    avformat_network_deinit();

    SDL_Quit();

}

bool VideoCtl::StartPlay(QString strFileName, WId widPlayWid)
{
    m_bPlayLoop = false;
    //先停止之前的播放线程
    if (m_tPlayLoopThread.joinable())
    {
        m_tPlayLoopThread.join();
    }
    emit SigStartPlay(strFileName);//正式播放，发送给标题栏

    play_wid = widPlayWid;

    VideoState* is;

    char file_name[1024];
    memset(file_name, 0, 1024);
    sprintf(file_name, "%s", strFileName.toLocal8Bit().data());
    //打开流
    is = stream_open(file_name);
    if (!is) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        do_exit(m_CurStream);
    }

    m_CurStream = is;

    //事件循环
    m_tPlayLoopThread = std::thread(&VideoCtl::LoopThread, this, is);

    return true;
}
