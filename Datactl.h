#pragma once
#ifdef _windows_
#include <windows.h>
#include <objbase.h>
#endif

#ifdef linux
#include <unistd.h>
#endif

#include <thread>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <assert.h>
#include "globalhelper.h"

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

//数据包列表
typedef struct MyAVPacketList {
	AVPacket pkt;	//解封装后的数据
	struct MyAVPacketList* next;	//下⼀个节点
	int serial;	//播放序列，每做⼀次seek，该serial都会做+1的递增，以区分不同的播放序列
} MyAVPacketList;

//数据包队列
typedef struct PacketQueue {
	MyAVPacketList* first_pkt, * last_pkt;	// 队⾸，队尾指针
	int nb_packets;	// 包数量，也就是队列元素数量
	int size;	// 队列所有元素的数据⼤⼩总和
	int64_t duration;	// 队列所有元素的数据播放持续时间
	int abort_request;	// ⽤户退出请求标志
	int serial;	// 播放序列号，和MyAVPacketList的serial作⽤相同
	SDL_mutex* mutex;	// ⽤于维持PacketQueue的多线程安全(SDL_mutex可以按pthread_mutex_t理解）
	SDL_cond* cond;	// ⽤于读、写线程相互通知(SDL_cond可以按pthread_cond_t理解)
} PacketQueue;

//音频参数
typedef struct AudioParams {
	int freq;	
	int channels;	
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
} AudioParams;

//时钟
typedef struct Clock {
	double pts;           // 时钟基础, 当前帧(待播放)显示时间戳，播放后，当前帧变成上⼀帧
	double pts_drift;     // pts与当前系统时钟的差值, audio、video对于该值是独⽴的，用于获取当前的显示的时间戳
	double last_updated;	// 当前时钟(如视频时钟)最后⼀次更新时间，也可称当前时钟时间
	double speed;	// 时钟速度控制，⽤于控制播放速度
	int serial;           // 播放序列，所谓播放序列就是⼀段连续的播放动作，⼀个seek操作会启动⼀段新的播放序列
	int paused;	// = 1 说明是暂停状态
	int* queue_serial;    // 指向packet_serial
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
//解码后的帧
typedef struct Frame {
	AVFrame* frame;	// 指向数据帧
	AVSubtitle sub;	// ⽤于字幕
	int serial;	// 播放序列，在seek的操作时serial会变化
	double pts;           // 时间戳，单位为秒
	double duration;      // 该帧持续时间，单位为秒
	int64_t pos;          // 该帧在输⼊⽂件中的字节位置
	int width;	// 图像宽度
	int height;	// 图像⾼读
	int format;	// 对于图像为(enum AVPixelFormat)，对于声⾳则为(enum AVSampleFormat)
	AVRational sar;	// 图像的宽⾼⽐，如果未知或未指定则为0/1
	int uploaded;	// 当前帧是否上传到GPU
	int flip_v;	// =1则旋转180， = 0则正常播放
} Frame;

//帧队列
typedef struct FrameQueue {
	Frame queue[FRAME_QUEUE_SIZE];	// FRAME_QUEUE_SIZE 最⼤size, 数字太⼤时会占⽤⼤量的内存，需要注意该值的设置
	int rindex;	// rindex指示已经显示的最近一帧（一般是未调用frame_queue_next之前以适应暂停时候显示画面或者窗口变化时作为参考帧）或正等待被显示的帧起始位置。frame_queue_next()会改变其位置;
	int windex;	// 写索引
	int size;	// size 并非表示内存大小，而是当前队列中帧的数量
	int max_size;	// 可存储最⼤帧数；认情况下，max_size 的值可能比实际的数组大小（如 FRAME_QUEUE_SIZE）小，以确保在高分辨率视频情况下不会占用过多内存
	int keep_last;	// 音频流与视频流都已经写死为1，字母流写死为0;keep_last 决定了在播放完一帧后，是否将其保留在队列中而不立即销毁。启用 keep_last 的主要目的是在需要重新渲染上一帧时（例如窗口大小变化）能够直接获取，而无需重新解码。
	int rindex_shown;	//默认情况下，rindex_shown 为 0，表示直接读取 rindex 所指向的帧。当启用了 keep_last 功能且 rindex_shown 被设置为 1 时，读取帧时会使用 rindex + rindex_shown 作为索引，以确保能够正确读取下一帧。​
	SDL_mutex* mutex;	// 互斥量
	SDL_cond* cond;	// 条件变量
	PacketQueue* pktq;	// 数据包缓冲队列
} FrameQueue;

enum {
	AV_SYNC_AUDIO_MASTER, /* default choice */
	AV_SYNC_VIDEO_MASTER,
	AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

//解码器，管理数据队列
typedef struct Decoder {
	AVPacket pkt;
	AVPacket pkt_temp;
	PacketQueue* queue;
	AVCodecContext* avctx;
	int pkt_serial;
	int finished;
	int packet_pending;
	SDL_cond* empty_queue_cond;	//外部总管VideoState传进来的
	int64_t start_pts;
	AVRational start_pts_tb;
	int64_t next_pts;
	AVRational next_pts_tb;
	std::thread decode_thread;
} Decoder;

//视频状态，管理所有的视频信息及数据
//仿照ffplay的结构体设计
typedef struct VideoState {
	std::thread read_tid; //读取线程
	AVInputFormat* iformat;	//指向demuxer，一般不指定
	int abort_request; //1-停止读取标志
	int force_refresh;	// = 1时需要刷新画⾯，请求⽴即刷新画⾯的意思
	int paused;	// =1时暂停，=0时播放
	int last_paused;	//// 暂存“暂停”/“播放”状态
	int queue_attachments_req;	
	int seek_req;	// 标识⼀次seek请求
	int seek_flags;	// seek标志，诸如AVSEEK_FLAG_BYTE等
	int64_t seek_pos;	// 请求seek的⽬标位置(当前位置+增量)
	int64_t seek_rel;	// >0-用户请求在目标位置之前;<0-用户希望在目标位置之后的一个区间内进行搜索
	int read_pause_return;
	AVFormatContext* ic;	// iformat的上下⽂
	int realtime;	// =1为实时流
	Clock audclk;	// ⾳频时钟
	Clock vidclk;	// 视频时钟
	Clock extclk;	// 外部时钟
	FrameQueue pictq;	// 视频Frame队列
	FrameQueue subpq;	// 字幕Frame队列
	FrameQueue sampq;	// 采样Frame队列
	Decoder auddec;	// ⾳频解码器
	Decoder viddec;	// 视频解码器
	Decoder subdec;	// 字幕解码器
	int audio_stream;	// ⾳频流索引
	int av_sync_type;	// ⾳视频同步类型, 默认audio master
	double audio_clock;	// 当前⾳频帧的PTS+当前帧Duration，即该帧（audio_buf）结束位置的时间戳（一段值）
	int audio_clock_serial;	// 播放序列，seek可改变此值
	// 以下4个参数 ⾮audio master同步⽅式使⽤
	double audio_diff_cum; /* used for AV difference average computation */
	double audio_diff_avg_coef;
	double audio_diff_threshold;
	int audio_diff_avg_count;
	// end
	AVStream* audio_st;	// ⾳频流
	PacketQueue audioq;	// ⾳频packet队列
	int audio_hw_buf_size;	//  SDL在内部为音频输出分配的缓冲区容量。
	// 指向待播放的⼀帧⾳频数据，指向的数据区将被拷⼊SDL⾳频缓冲区。
	// 若经过重采样则指向audio_buf1，否则指向frame中的⾳频
	uint8_t* audio_buf;	// 指向需要重采样的数据（指向音频数据缓冲区的指针）
	uint8_t* audio_buf1;	// 指向重采样后的数据
	// 待播放的⼀帧⾳频数据(audio_buf指向)的⼤⼩（字节）
	unsigned int audio_buf_size; 
	// 申请到的⾳频缓冲区audio_buf1的实际尺⼨
	unsigned int audio_buf1_size;
	// 指示当前已处理或播放的音频数据在 audio_buf 中的偏移量（以字节为单位）。
	int audio_buf_index; 
	// 当前⾳频帧中尚未拷⼊SDL⾳频缓冲区的数据量:
	// audio_buf_size = audio_buf_index + audio_write_buf_size
	int audio_write_buf_size;
	int audio_volume;	// ⾳量
	struct AudioParams audio_src;	// ⾳频frame的参数
	struct AudioParams audio_tgt;	// SDL⽀持的⾳频参数，重采样转换：audio_src->audio_tgt
	struct SwrContext* swr_ctx;	// ⾳频重采样context
	int frame_drops_early;	// 丢弃视频packet计数
	int frame_drops_late;	// 丢弃视频frame计数
	// ⾳频波形显示使⽤
	int16_t sample_array[SAMPLE_ARRAY_SIZE];
	int sample_array_index;
	int last_i_start;
	RDFTContext* rdft;
	int rdft_bits;
	FFTSample* rdft_data;
	//end
	int xpos;
	double last_vis_time;
	SDL_Texture* sub_texture;	// 字幕显示
	SDL_Texture* vid_texture;	// 视频显示
	int subtitle_stream;	// 字幕流索引
	AVStream* subtitle_st;	// 字幕流
	PacketQueue subtitleq;	// 字幕packet队列
	double frame_timer;	// 记录最后一帧播放到目前的时刻
	double frame_last_returned_time;
	double frame_last_filter_delay;
	int video_stream;// 视频流索引
	AVStream* video_st;	// 视频流
	PacketQueue videoq;	// 视频队列
	double max_frame_duration;      // ⼀帧最⼤间隔 - above this, we consider the jump a timestamp discontinuity
	struct SwsContext* img_convert_ctx;	// 视频尺⼨格式变换
	struct SwsContext* sub_convert_ctx;	// 字幕尺⼨格式变换
	int eof;	// 是否读取结束
	char* filename;	// ⽂件名
	int width, height, xleft, ytop;	// 宽、⾼，x起始坐标，y起始坐标
	int step;	// =1 步进播放模式, =0 其他模式（step 模式下用户可能希望逐帧观察，不允许自动丢帧）。
	// 保留最近的相应audio、video、subtitle流的steam index
	int last_video_stream, last_audio_stream, last_subtitle_stream;
	SDL_cond* continue_read_thread;	// 当读取数据队列满了后进⼊休眠时，可以通过该condition唤醒读线程
} VideoState;

//缓冲包
static AVPacket flush_pkt;

//数据包队列存放数据包（供队列内部使用）
static int packet_queue_put_private(PacketQueue* q, AVPacket* pkt)
{
	MyAVPacketList* pkt1;

	if (q->abort_request)
		return -1;

	pkt1 = (MyAVPacketList*)av_malloc(sizeof(MyAVPacketList));
	if (!pkt1)
		return -1;
	// 没有做引⽤计数，那这⾥也说明av_read_frame不会释放替⽤户释放buffer。
	//拷⻉AVPacket(浅拷⻉，AVPacket.data等内存并没有拷贝)
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	//如果放⼊的是flush_pkt，需要增加队列的播放序列号，以区分不连续的两段数据
	if (pkt == &flush_pkt)
		q->serial++;
	pkt1->serial = q->serial;

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size + sizeof(*pkt1);
	q->duration += pkt1->pkt.duration;
	/* XXX: should duplicate packet data in DV case */
	SDL_CondSignal(q->cond);
	return 0;
}

//数据包队列存放数据包
static int packet_queue_put(PacketQueue* q, AVPacket* pkt)
{
	int ret;

	SDL_LockMutex(q->mutex);
	ret = packet_queue_put_private(q, pkt);
	SDL_UnlockMutex(q->mutex);

	if (pkt != &flush_pkt && ret < 0)
		av_packet_unref(pkt);

	return ret;
}

//数据包队列存放空数据包
static int packet_queue_put_nullpacket(PacketQueue* q, int stream_index)
{
	AVPacket pkt1, * pkt = &pkt1;
	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;
	pkt->stream_index = stream_index;
	return packet_queue_put(q, pkt);
}

/// <summary>
/// 数据包队列初始化
/// </summary>
/// <param name="q">：is->的AVPacket队列</param>
/// <returns>0-成功</returns>
static int packet_queue_init(PacketQueue* q)
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	if (!q->mutex) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	q->cond = SDL_CreateCond();
	if (!q->cond) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	q->abort_request = 1;
	return 0;
}

//数据包队列清空
static void packet_queue_flush(PacketQueue* q)
{
	MyAVPacketList* pkt, * pkt1;

	SDL_LockMutex(q->mutex);
	for (pkt = q->first_pkt; pkt; pkt = pkt1) {
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->duration = 0;
	SDL_UnlockMutex(q->mutex);
}

//数据包队列销毁
static void packet_queue_destroy(PacketQueue* q)
{
	//先清除所有的节点
	packet_queue_flush(q);
	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCond(q->cond);
}

//数据包队列停用
static void packet_queue_abort(PacketQueue* q)
{
	SDL_LockMutex(q->mutex);

	q->abort_request = 1;	//请求退出

	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
}

//数据包队列开始使用
static void packet_queue_start(PacketQueue* q)
{
	//初始化清理包
	av_init_packet(&flush_pkt);
	flush_pkt.data = (uint8_t*)&flush_pkt;
	SDL_LockMutex(q->mutex);
	q->abort_request = 0;
	//放入了一个flush_pkt，目的新增serial以区分之前的队列，触发解码器清空⾃身缓存 avcodec_flush_buffers()
	packet_queue_put_private(q, &flush_pkt);	
	SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
//从数据包队列中获取数据包
static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block, int* serial)
{
	MyAVPacketList* pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {
		if (q->abort_request) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size + sizeof(*pkt1);
			q->duration -= pkt1->pkt.duration;
			//返回AVPacket，这⾥发⽣⼀次AVPacket结构体拷⻉，AVPacket的data只拷贝了指针
			*pkt = pkt1->pkt;
			//如果需要输出serial，把serial输出
			if (serial)	
				*serial = pkt1->serial;
			//释放节点内存,只是释放节点，⽽不是释放AVPacket
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {	//队列中没有数据，且⾮阻塞调⽤
			ret = 0;
			break;
		}
		else {
			//队列中没有数据，且阻塞调⽤
			//这⾥没有break。for循环的另⼀个作⽤是在条件变量满⾜后重复上述代码取出节点
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

//解码器初始化（绑定解码结构体、数据包队列、信号量，初始化pts）
static void decoder_init(Decoder* d, AVCodecContext* avctx, PacketQueue* queue, SDL_cond* empty_queue_cond) {
	memset(d, 0, sizeof(Decoder));
	d->avctx = avctx;
	d->queue = queue;
	d->empty_queue_cond = empty_queue_cond;
	d->start_pts = AV_NOPTS_VALUE;
}

static int decoder_reorder_pts = -1;
#if 0
//解码一帧数据
static int decoder_decode_frame(Decoder* d, AVFrame* frame, AVSubtitle* sub) {
	int got_frame = 0;

	do {
		int ret = -1;

		if (d->queue->abort_request)
			return -1;

		if (!d->packet_pending || d->queue->serial != d->pkt_serial) {
			AVPacket pkt;
			do {
				if (d->queue->nb_packets == 0)
					SDL_CondSignal(d->empty_queue_cond);
				//从对应的队列中获取原始数据
				if (packet_queue_get(d->queue, &pkt, 1, &d->pkt_serial) < 0)
					return -1;
				if (pkt.data == flush_pkt.data) {
					avcodec_flush_buffers(d->avctx);
					d->finished = 0;
					d->next_pts = d->start_pts;
					d->next_pts_tb = d->start_pts_tb;
				}
			} while (pkt.data == flush_pkt.data || d->queue->serial != d->pkt_serial);
			av_packet_unref(&d->pkt);
			d->pkt_temp = d->pkt = pkt;
			d->packet_pending = 1;
		}

		switch (d->avctx->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			//解码视频帧
			ret = avcodec_decode_video2(d->avctx, frame, &got_frame, &d->pkt_temp);
			if (got_frame) {
				if (decoder_reorder_pts == -1) {
					frame->pts = av_frame_get_best_effort_timestamp(frame);
				}
				else if (!decoder_reorder_pts) {
					frame->pts = frame->pkt_dts;
				}
			}
			break;
		case AVMEDIA_TYPE_AUDIO:
			//解码音频帧
			ret = avcodec_decode_audio4(d->avctx, frame, &got_frame, &d->pkt_temp);
			if (got_frame) {
				//AVRational tb = (AVRational) { 1, frame->sample_rate };
				AVRational tb = { 1, frame->sample_rate };
				if (frame->pts != AV_NOPTS_VALUE)
					frame->pts = av_rescale_q(frame->pts, av_codec_get_pkt_timebase(d->avctx), tb);
				else if (d->next_pts != AV_NOPTS_VALUE)
					frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
				if (frame->pts != AV_NOPTS_VALUE) {
					d->next_pts = frame->pts + frame->nb_samples;
					d->next_pts_tb = tb;
				}
			}
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			//解码字幕帧
			ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &d->pkt_temp);
			break;
		}

		if (ret < 0) {
			d->packet_pending = 0;
		}
		else {
			d->pkt_temp.dts =
				d->pkt_temp.pts = AV_NOPTS_VALUE;
			if (d->pkt_temp.data) {
				if (d->avctx->codec_type != AVMEDIA_TYPE_AUDIO)
					ret = d->pkt_temp.size;
				d->pkt_temp.data += ret;
				d->pkt_temp.size -= ret;
				if (d->pkt_temp.size <= 0)
					d->packet_pending = 0;
			}
			else {
				if (!got_frame) {
					d->packet_pending = 0;
					d->finished = d->pkt_serial;
				}
			}
		}
	} while (!got_frame && !d->finished);

	return got_frame;
}
#endif
#if 1
static int decoder_decode_frame(Decoder* d, AVFrame* frame, AVSubtitle* sub) {
	int ret = AVERROR(EAGAIN);
	for (;;) {
		AVPacket pkt;
		// 1. 流连续情况下获取解码后的帧
		if (d->queue->serial == d->pkt_serial) { // 1.1 先判断是否是同一播放序列的数据
			do {
				if (d->queue->abort_request)
					return -1;  // 是否请求退出
				// 1.2. 获取解码帧
				switch (d->avctx->codec_type) {
				case AVMEDIA_TYPE_VIDEO:
					ret = avcodec_receive_frame(d->avctx, frame);
					//printf("frame pts:%ld, dts:%ld\n", frame->pts, frame->pkt_dts);
					if (ret >= 0) {
						if (decoder_reorder_pts == -1) {
							frame->pts = frame->best_effort_timestamp;
						}
						else if (!decoder_reorder_pts) {
							frame->pts = frame->pkt_dts;
						}
					}
					break;
				case AVMEDIA_TYPE_AUDIO:
					ret = avcodec_receive_frame(d->avctx, frame);
					if (ret >= 0) {
						AVRational tb = { 1, frame->sample_rate };    //
						if (frame->pts != AV_NOPTS_VALUE) {
							// 如果frame->pts正常则先将其从pkt_timebase转成{1, frame->sample_rate}
							// pkt_timebase实质就是stream->time_base
							frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
						}
						else if (d->next_pts != AV_NOPTS_VALUE) {
							// 如果frame->pts不正常则使用上一帧更新的next_pts和next_pts_tb
							// 转成{1, frame->sample_rate}
							frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
						}
						if (frame->pts != AV_NOPTS_VALUE) {
							// 根据当前帧的pts和nb_samples预估下一帧的pts
							d->next_pts = frame->pts + frame->nb_samples;
							d->next_pts_tb = tb; // 设置timebase
						}
					}
					break;
				}
				// 1.3. 检查解码是否已经结束，解码结束返回0
				if (ret == AVERROR_EOF) {
					d->finished = d->pkt_serial;
					printf("avcodec_flush_buffers %s(%d)\n", __FUNCTION__, __LINE__);
					avcodec_flush_buffers(d->avctx);
					return 0;
				}
				// 1.4. 正常解码返回1
				if (ret >= 0)
					return 1;
			} while (ret != AVERROR(EAGAIN));   // 1.5 没帧可读时ret返回EAGIN，需要继续送packet
		}
		// 2 获取一个packet，如果播放序列不一致(数据不连续)则过滤掉“过时”的packet
		do {
			// 2.1 如果没有数据可读则唤醒read_thread, 实际是continue_read_thread SDL_cond
			if (d->queue->nb_packets == 0)  // 没有数据可读
				SDL_CondSignal(d->empty_queue_cond);// 通知read_thread放入packet
			// 2.2 如果还有pending的packet则使用它
			if (d->packet_pending) {
				av_packet_move_ref(&pkt, &d->pkt);
				d->packet_pending = 0;
			}
			else {
				// 2.3 阻塞式读取packet
				if (packet_queue_get(d->queue, &pkt, 1, &d->pkt_serial) < 0)
					return -1;
			}
			if (d->queue->serial != d->pkt_serial) {
				printf("%s(%d) discontinue:queue->serial:%d,pkt_serial:%d\n",
					__FUNCTION__, __LINE__, d->queue->serial, d->pkt_serial);
				av_packet_unref(&pkt); // fixed me? 释放要过滤的packet
			}
		} while (d->queue->serial != d->pkt_serial);// 如果不是同一播放序列(流不连续)则继续读取

		// 3 将packet送入解码器
		if (pkt.data == flush_pkt.data) {//
			// when seeking or when switching to a different stream
			avcodec_flush_buffers(d->avctx); //清空里面的缓存帧
			d->finished = 0;        // 重置为0
			d->next_pts = d->start_pts;     // 主要用在了audio
			d->next_pts_tb = d->start_pts_tb;// 主要用在了audio
		}
		else {
			if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
				int got_frame = 0;
				ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &pkt);
				if (ret < 0) {
					ret = AVERROR(EAGAIN);
				}
				else {
					if (got_frame && !pkt.data) {
						d->packet_pending = 1;
						av_packet_move_ref(&d->pkt, &pkt);
					}
					ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
				}
			}
			else {
				if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {
					av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
					d->packet_pending = 1;
					av_packet_move_ref(&d->pkt, &pkt);
				}
			}
			av_packet_unref(&pkt);	// 一定要自己去释放音视频数据
		}
	}
}
#endif

//解码器销毁
static void decoder_destroy(Decoder* d) {
	av_packet_unref(&d->pkt);
	avcodec_free_context(&d->avctx);
}

static void frame_queue_unref_item(Frame* vp)
{
	av_frame_unref(vp->frame);
	avsubtitle_free(&vp->sub);
}

/// <summary>
/// 帧队列初始化（绑定数据包队列，初始化最大值），并且为Frame数组分配AVFrame空间
/// </summary>
/// <param name="f">：is->AVFrame队列</param>
/// <param name="pktq">：is->AVPacket队列</param>
/// <param name="max_size">：AVFrame队列的初始设定[视频：VIDEO_PICTURE_QUEUE_SIZE=3；音频：SAMPLE_QUEUE_SIZE=9]</param>
/// <param name="keep_last">：默认传值1</param>
/// <returns>0-成功</returns>
static int frame_queue_init(FrameQueue* f, PacketQueue* pktq, int max_size, int keep_last)
{
	int i;
	memset(f, 0, sizeof(FrameQueue));
	if (!(f->mutex = SDL_CreateMutex())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	if (!(f->cond = SDL_CreateCond())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	f->pktq = pktq;
	f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
	f->keep_last = !!keep_last;
	//为队列中所有的缓存帧预先申请内存
	for (i = 0; i < f->max_size; i++)
		if (!(f->queue[i].frame = av_frame_alloc()))
			return AVERROR(ENOMEM);
	return 0;
}

//帧队列销毁
static void frame_queue_destory(FrameQueue* f)
{
	int i;
	for (i = 0; i < f->max_size; i++) {
		Frame* vp = &f->queue[i];
		frame_queue_unref_item(vp);
		av_frame_free(&vp->frame);
	}
	SDL_DestroyMutex(f->mutex);
	SDL_DestroyCond(f->cond);
}

//帧队列信号
static void frame_queue_signal(FrameQueue* f)
{
	SDL_LockMutex(f->mutex);
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}
/// <summary>
/// 该函数返回当前可显示的帧，即读索引加上显示偏移量所指向的帧。​这对于在保留最后一帧的情况下，确保获取到正确的帧进行显示非常重要。
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static Frame* frame_queue_peek(FrameQueue* f)
{
	return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
/// <summary>
/// 该函数返回在当前显示帧的基础上再前进一个位置。​这在需要预取或预览下一帧内容时非常有用。
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static Frame* frame_queue_peek_next(FrameQueue* f)
{
	return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}
/// <summary>
/// 该函数返回最后一个已显示的帧，即当前读索引所指向的帧。​这对于需要重新渲染或重复显示最后一帧的场景非常有用。
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static Frame* frame_queue_peek_last(FrameQueue* f)
{
	return &f->queue[f->rindex];
}
/// <summary>
/// 该函数用于获取一个可写入的新帧位置。​如果帧队列已满，函数会等待直到有空间可写或接收到中止请求。​这确保了生产者线程在队列满时不会覆盖未处理的帧。​
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static Frame* frame_queue_peek_writable(FrameQueue* f)
{
	/* wait until we have space to put a new frame */
	SDL_LockMutex(f->mutex);
	while (f->size >= f->max_size &&!f->pktq->abort_request) {
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);
	if (f->pktq->abort_request)
		return NULL;
	return &f->queue[f->windex];
}
/// <summary>
/// 该函数用于获取一个可读取的帧。​如果没有可读帧，函数会等待直到有新帧可读或接收到中止请求。​这确保了消费者线程在没有可用帧时不会读取无效数据。
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static Frame* frame_queue_peek_readable(FrameQueue* f)
{
	/* wait until we have a readable a new frame */
	SDL_LockMutex(f->mutex);
	while (f->size - f->rindex_shown <= 0 &&
		!f->pktq->abort_request) {
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);

	if (f->pktq->abort_request)
		return NULL;

	return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
/// <summary>
/// 该函数在帧被写入后调用，移动写索引并更新队列大小。​如果写索引达到最大值，则循环回到起始位置。​同时，函数会发出信号通知可能等待的读取操作。
/// </summary>
/// <param name="f"></param>
static void frame_queue_push(FrameQueue* f)
{
	if (++f->windex == f->max_size)
		f->windex = 0;
	SDL_LockMutex(f->mutex);
	f->size++;
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}
/// <summary>
/// 该函数在帧被消费后调用，移动读索引并更新队列大小。​如果设置了 keep_last，则保留最后一帧以供重复显示。​同时，函数会发出信号通知可能等待的写入操作。
/// </summary>
/// <param name="f"></param>
static void frame_queue_next(FrameQueue* f)
{
	//start
	//第一帧会进入，此后不会进入
	if (f->keep_last && !f->rindex_shown) {
		f->rindex_shown = 1;
		return;
	}
	//end
	frame_queue_unref_item(&f->queue[f->rindex]);
	if (++f->rindex == f->max_size)
		f->rindex = 0;
	SDL_LockMutex(f->mutex);
	f->size--;
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

/// <summary>
/// 该函数返回队列中尚未显示的帧数。​这对于了解当前缓冲区的填充程度和播放进度非常有用。
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static int frame_queue_nb_remaining(FrameQueue* f)
{
	//减去队列中已经显示的但还没有出队列的rindex一帧
	return f->size - f->rindex_shown;
}

/// <summary>
/// 该函数返回最后显示帧的位置（如字节位置或时间戳），用于同步和调试目的。​如果没有可用的位置信息，函数返回 -1。
/// </summary>
/// <param name="f"></param>
/// <returns></returns>
static int64_t frame_queue_last_pos(FrameQueue* f)
{
	Frame* fp = &f->queue[f->rindex];
	if (f->rindex_shown && fp->serial == f->pktq->serial)
		return fp->pos;
	else
		return -1;
}

static void decoder_abort(Decoder* d, FrameQueue* fq)
{
	packet_queue_abort(d->queue);
	frame_queue_signal(fq);
	d->decode_thread.join();
	packet_queue_flush(d->queue);
}
