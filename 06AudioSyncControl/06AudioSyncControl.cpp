// tutorial06.c
// A pedagogical video player that really works!
//
// This tutorial was written by Stephen Dranger (dranger@gmail.com).
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
//
//
//
// Updates tested on:
// Mac OS X 10.11.6
// Apple LLVM version 8.0.0 (clang-800.0.38)
//
// Use 
//
// $ gcc -o tutorial06 tutorial06.c -lavutil -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
//
// to build (assuming libavutil/libavformat/libavcodec/libswscale are correctly installed your system).
//
// Run using
//
// $ tutorial06 myvideofile.mpg
//
// to play the video.
#include "SDL.h"
extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavformat/avio.h"
#include "libavutil/avstring.h"
#include "libavutil/time.h"
}
//#undef main
#pragma comment(lib,"SDL.lib")
#pragma comment(lib,"SDLmain.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avdevice.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"postproc.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#ifdef __MINGW32__
#undef main // Prevents SDL from overriding main().
#endif

#include <stdio.h>
#include <math.h>
const char* fileName = "../data/test.mp4";

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01//ǰ����֡�����ʾʱ��������Сֵ0.01s
#define AV_NOSYNC_THRESHOLD 10.0//��Сˢ�¼��ʱ��10ms

#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

#define FF_ALLOC_EVENT (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

#define VIDEO_PICTURE_QUEUE_SIZE 1

#define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER//ָ������Ƶʱ��Ϊ��ͬ��Դ(ʱ���׼)

SDL_Surface* screen;//SDL��ͼ���棬A structure that contains a collection of pixels used in software blitting
uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;

enum {//ͬ��ʱ��Դ
	AV_SYNC_AUDIO_MASTER,//��Ƶʱ��Ϊ��ͬ��Դ
	AV_SYNC_VIDEO_MASTER,//��Ƶʱ��Ϊ��ͬ��Դ
	AV_SYNC_EXTERNAL_MASTER,//�ⲿʱ��Ϊ��ͬ��Դ
};

/*-------ȡ��ϵͳ��ǰʱ��--------
int64_t av_gettime(void){
#if defined(CONFUG_WINCE)
	return timeGetTime()*int64_t_C(1000);
#elif defined(CONFIG_WIN32)
	struct _timeb tb;
	_ftime(&tb);
	return ((int64_t)tb.time*int64_t_C(1000)+(int64_t)tb.millitm)*int64_t_C(1000);
#else
	struct timeval tv;
	gettimeofday(&tv,NULL);//ȡ��ϵͳ��ǰʱ��
	return (int64_t)tv.tv_sec*1000000+tv.tv_usec;//��1/1000000��Ϊ��λ�������ڸ���ƽ̨��ֲ
#endif
}
---------------------------*/

/*-------����ڵ�ṹ��--------
typedef struct AVPacketList {
	AVPacket pkt;//��������
	struct AVPacketList *next;//�����̽ڵ�
} AVPacketList;
---------------------------*/
//���ݰ�����(����)�ṹ��
typedef struct PacketQueue {
	AVPacketList* first_pkt, * last_pkt;//������β�ڵ�ָ��
	int nb_packets;//���г���
	int size;//����������ݵĻ��泤�ȣ�size=packet->size
	SDL_mutex* qlock;//���л�������������������
	SDL_cond* qready;//���о�����������
} PacketQueue;

//ͼ��ͼ��ṹ��
typedef struct VideoPicture {
	SDL_Overlay* bmp;//SDL����overlay
	int width, height;//Source height & width
	int allocated;//�Ƿ�����ڴ�ռ䣬��Ƶ֡ת��ΪSDL overlay��ʶ
	double pts;//��ǰͼ��֡�ľ�����ʾʱ���
} VideoPicture;

//�������ݼ��������ڲ��ſ��Ƶ����в���������һ�𣬱����ڸ���������ʹ��
typedef struct VideoState {
	AVFormatContext* pFormatCtx;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVPacket audio_pkt;//����Ӷ�������ȡ�����ݰ�
	AVFrame audio_frame;//��������ݰ��н������Ƶ����
	AVStream* video_st;//��Ƶ����Ϣ�ṹ��ָ��
	AVStream* audio_st;//��Ƶ����Ϣ�ṹ��ָ��
	struct SwsContext* sws_ctx;//����ת���������Ľṹ��
	AVIOContext* io_context;

	PacketQueue videoq;//��Ƶ�������ݰ�����(�������ݶ��У�������ʽʵ��)
	//������ͼ��֡����(�������ݶ��У������鷽ʽʵ��)����Ⱦ�߼��ͻ��pictq��ȡ���ݣ�ͬʱ�����߼��ֻ���pictqд������
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex, pictq_windex;//���г��ȣ���/дλ������
	SDL_mutex* pictq_lock;//���ж�д�����󣬱���ͼ��֡��������
	SDL_cond* pictq_ready;//���о�����������

	PacketQueue audioq;//��Ƶ�������ݰ�����(�������ݶ��У�������ʽʵ��)
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];//�������һ��packet��Ķ�֡ԭʼ��Ƶ����(�������ݶ��У������鷽ʽʵ��)
	unsigned int audio_buf_size;//�����Ķ�֡��Ƶ���ݳ���
	unsigned int audio_buf_index;//�ۼ�д����������ĳ���(�������Ƶ���ݴ�С)
	uint8_t* audio_pkt_data;//�������ݻ���ָ��λ��
	int audio_pkt_size;//������ʣ��ı������ݳ���(�Ƿ������һ��������pakcet���Ľ��룬һ�����ݰ��п��ܰ��������Ƶ����֡)
	int audio_hw_buf_size;

	int videoStream, audioStream;//����Ƶ�����ͱ��
	SDL_Thread* parse_tid;//���ݰ������߳�id(demux�⸴���߳�id)
	SDL_Thread* video_tid;//�����߳�id

	char filename[1024];//�����ļ�����·����
	int quit;//ȫ���˳����̱�ʶ���ڽ����ϵ����˳��󣬸����߳��˳�

	//video/audio_clock save pts of last decoded frame/predicted pts of next decoded frame
	double video_clock;//keep track of how much time has passed according to the video ��Ƶ�Ѳ���ʱ�䣬����Ԥ����һ֡��̬ˢ�µ��ӳ�ʱ��
	double audio_clock;//��Ƶ�Ѳ���ʱ��
	double frame_timer;//��Ƶ���ŵ���ǰ֡ʱ���ۼ��Ѳ���ʱ��
	double frame_last_pts;//��һ֡ͼ�����ʾʱ�����������video_refersh_timer�б�����һ֡��ptsֵ
	double frame_last_delay;//��һ֡ͼ��Ķ�̬ˢ���ӳ�ʱ��

	int av_sync_type;//��ͬ��Դ����
	double external_clock;//External clock base
	int64_t external_clock_time;//�ⲿʱ�ӵľ���ʱ��
	double audio_diff_cum;//��Ƶʱ����ͬ��Դ�ۼ�ʱ�sed for AV difference average computation
	double audio_diff_avg_coef;//��Ƶʱ����ͬ��Դʱ���ֵ��Ȩϵ��
	double audio_diff_threshold;//��Ƶʱ����ͬ��Դʱ���ֵ��ֵ
	int audio_diff_avg_count;//��Ƶ��ͬ������(��Ƶʱ������ͬ��Դ���ڲ�ͬ���Ĵ���)

	double video_current_pts;//��ǰ֡��ʾʱ�����Current displayed pts (different from video_clock if frame fifos are used)
	int64_t video_current_pts_time;//ȡ��video_current_pts��ϵͳʱ�䣬time (av_gettime) at which we updated video_current_pts - used to have running video pts
} VideoState;// Since we only have one decoding thread, the Big Struct can be global in case we need it
VideoState* global_video_state;

//���ݰ����г�ʼ������
void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));//ȫ���ʼ�����нṹ�����
	q->qlock = SDL_CreateMutex();//��������������
	q->qready = SDL_CreateCond();//����������������
}

//������в������ݰ�
int packet_queue_put(PacketQueue* q, AVPacket* pkt) {
	/*-------׼������(����)�ڵ����------*/
	AVPacketList* pktlist = (AVPacketList*)av_malloc(sizeof(AVPacketList));//�ڶ��ϴ�������ڵ����
	if (!pktlist) {//�������ڵ�����Ƿ񴴽��ɹ�
		return -1;
	}
	pktlist->pkt = *pkt;//���������ݰ���ֵ���½�����ڵ�����е����ݰ�����
	pktlist->next = NULL;//������ָ��Ϊ��
//	if (av_packet_ref(pkt, pkt) < 0) {
//		return -1;
//	}
/*---------���½��ڵ�������-------*/
	SDL_LockMutex(q->qlock);//���л�����������������������

	if (!q->last_pkt) {//������β�ڵ��Ƿ����(�������Ƿ�Ϊ��)
		q->first_pkt = pktlist;//��������(����β��)���򽫵�ǰ�ڵ�������Ϊ�׽ڵ�
	}
	else {
		q->last_pkt->next = pktlist;//���Ѵ���β�ڵ㣬�򽫵�ǰ�ڵ�ҵ�β�ڵ�ĺ��ָ���ϣ�����Ϊ�µ�β�ڵ�
	}
	q->last_pkt = pktlist;//����ǰ�ڵ���Ϊ�µ�β�ڵ�
	q->nb_packets++;//���г���+1
	q->size += pktlist->pkt.size;//���¶��б������ݵĻ��泤��
	SDL_CondSignal(q->qready);//���ȴ��̷߳�����Ϣ��֪ͨ�����Ѿ���
	SDL_UnlockMutex(q->qlock);//�ͷŻ�����
	return 0;
}

//�Ӷ�������ȡ���ݰ���������ȡ�����ݰ�������
static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block) {
	AVPacketList* pktlist;//��ʱ����ڵ����ָ��
	int ret;//�������

	SDL_LockMutex(q->qlock);//���л�����������������������
	for (;;) {
		if (global_video_state->quit) {//����˳����̱�ʶ
			ret = -1;
			break;
		}//end for if

		pktlist = q->first_pkt;//���ݽ������׸����ݰ�ָ��
		if (pktlist) {//������ݰ��Ƿ�Ϊ��(�����Ƿ�������)
			q->first_pkt = pktlist->next;//�����׽ڵ�ָ�����
			if (!q->first_pkt) {//����׽ڵ�ĺ�̽ڵ��Ƿ����
				q->last_pkt = NULL;//�������ڣ���β�ڵ�ָ���ÿ�
			}
			q->nb_packets--;//���г���-1
			q->size -= pktlist->pkt.size;//���¶��б������ݵĻ��泤��
			*pkt = pktlist->pkt;//�������׽ڵ����ݷ���
			av_free(pktlist);//�����ʱ�ڵ�����(����׽ڵ����ݣ��׽ڵ������)
			ret = 1;//�����ɹ�
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {//���д���δ����״̬����ʱͨ��SDL_CondWait�����ȴ�qready�����źţ�����ʱ�Ի���������
		 /*---------------------
		  * �ȴ����о����ź�qready�����Ի�������ʱ����
		  * ��ʱ�̴߳�������״̬�������ڵȴ������������߳��б���
		  * ʹ�ø��߳�ֻ���ٽ�����Դ������ű����ѣ����������̱߳�Ƶ���л�
		  * �ú�������ʱ���������ٴα���ס����ִ�к�������
		  --------------------*/
			SDL_CondWait(q->qready, q->qlock);//��ʱ���������������Լ��������ȴ��ٽ�����Դ����(�ȴ�SDL_CondSignal�����ٽ�����Դ�������ź�)
		}
	}
	SDL_UnlockMutex(q->qlock);//�ͷŻ�����
	return ret;
}

//��Ƶ���뺯�����ӻ����������ȡ���ݰ������룬�����ؽ��������ݳ���(��һ��������packet���룬����������д��audio_buf���棬�����ض�֡�������ݵ��ܳ���)
int audio_decode_frame(VideoState* is, double* pts_ptr) {
	int coded_consumed_size, data_size = 0, pcm_bytes;//ÿ�����ĵı������ݳ���[input](len1)�����ԭʼ��Ƶ���ݵĻ��泤��[output]��ÿ����Ƶ�������ݵ��ֽ���
	AVPacket* pkt = &is->audio_pkt;//����Ӷ�������ȡ�����ݰ�
	double pts;//��Ƶ����ʱ���

	for (;;) {
		/*--2�������ݰ�pkt�в��ϵĽ�����Ƶ���ݣ�ֱ��ʣ��ı������ݳ���<=0---*/
		while (is->audio_pkt_size > 0) {//��黺����ʣ��ı������ݳ���(�Ƿ������һ��������pakcet���Ľ��룬һ�����ݰ��п��ܰ��������Ƶ����֡)
			int got_frame = 0;//��������ɹ���ʶ���ɹ����ط���ֵ
			//����һ֡��Ƶ���ݣ����������ĵı������ݳ���
			coded_consumed_size = avcodec_decode_audio4(is->audio_st->codec, &is->audio_frame, &got_frame, pkt);
			if (coded_consumed_size < 0) {//����Ƿ�ִ���˽������
				// if error, skip frame.
				is->audio_pkt_size = 0;//���±������ݻ��泤��
				break;
			}
			if (got_frame) {//����������Ƿ�ɹ�
				//����������Ƶԭʼ���ݳ���[output]
				data_size = av_samples_get_buffer_size(NULL, is->audio_st->codec->channels,
					is->audio_frame.nb_samples, is->audio_st->codec->sample_fmt, 1);
				memcpy(is->audio_buf, is->audio_frame.data[0], data_size);//���������ݸ��Ƶ��������
			}
			is->audio_pkt_data += coded_consumed_size;//���±������ݻ���ָ��λ��
			is->audio_pkt_size -= coded_consumed_size;//���»�����ʣ��ı������ݳ���
			if (data_size <= 0) {//�������������ݻ��泤��
				// No data yet, get more frames.
				continue;
			}
			pts = is->audio_clock;//��ÿ�θ��µ���Ƶ����ʱ�������ƵPTS
			*pts_ptr = pts;
			/*---------------------
			 * ��һ��packet�а��������Ƶ֡ʱ
			 * ͨ��[�������Ƶԭʼ���ݳ���]��[������]������һ��packet��������Ƶ֡�Ĳ���ʱ���pts
			 * ����Ƶ��44.1kHz������λ��16λ����ζ��ÿ��ɼ�����44.1k����ÿ������ռ2�ֽ�
			 --------------------*/
			pcm_bytes = 2 * is->audio_st->codec->channels;//����ÿ����Ƶ�������ݵ��ֽ���=ÿ��������Ƶ�����ֽ���*������
			/*----����audio_clock---
			 * һ��pkt���������Ƶframe��ͬʱһ��pkt��Ӧһ��pts(pkt->pts)
			 * ��ˣ���pkt�а����Ķ����Ƶ֡��ʱ��������¹�ʽ�ƶϵó�
			 * bytes_per_sec=pcm_bytes*is->audio_st->codec->sample_rate
			 * ��pkt�в��ϵĽ��룬�ƶ�(һ��pkt��)ÿ֡���ݵ�pts���ۼӵ���Ƶ����ʱ��
			 --------------------*/
			is->audio_clock += (double)data_size / (double)(pcm_bytes * is->audio_st->codec->sample_rate);
			// We have data, return it and come back for more later.
			return data_size;//���ؽ�������ԭʼ���ݳ���
		}
		/*-----------------1���ӻ����������ȡ���ݰ�------------------*/
		if (pkt->data) {//������ݰ��ǲ�����������
			av_packet_unref(pkt);//�ͷ�pkt�б���ı�������
		}
		if (is->quit) {//����˳����̱�ʶ
			return -1;
		}
		// Next packet���Ӷ�������ȡ���ݰ���pkt
		if (packet_queue_get(&is->audioq, pkt, 1) < 0) {
			return -1;
		}
		is->audio_pkt_data = pkt->data;//���ݱ������ݻ���ָ��
		is->audio_pkt_size = pkt->size;//���ݱ������ݻ��泤��
		// If update, update the audio clock w/pts.
		if (pkt->pts != AV_NOPTS_VALUE) {//�����Ƶ����ʱ���
			//���һ���µ�packet��ʱ�򣬸���audio_clock����packet�е�pts����audio_clock(һ��pkt��Ӧһ��pts)
			is->audio_clock = av_q2d(is->audio_st->time_base) * pkt->pts;//������Ƶ�Ѿ�����ʱ��
		}
	}
}
int synchronize_audio(VideoState* is, short* samples, int samples_size, double pts);
/*------Audio Callback-------
 * ��Ƶ����ص�������ϵͳͨ���ûص�������������pcm����������������,
 * ϵͳͨ��һ�λ�׼��һ�黺��pcm����(���ٵ���ϵͳi/o����)��ͨ���ûص���������������������Ƶpts���β���pcm����
 * �����뻺���pcm������ɲ��ź�������һ���µ�pcm��������(ÿ����Ƶ�������Ϊ��ʱ��ϵͳ�͵��ô˺��������Ƶ������棬��������������)
 * The audio function callback takes the following parameters:
 * stream: A pointer to the audio buffer to be filled�������Ƶ���ݵ���������
 * len: The length (in bytes) of the audio buffer,���泤��wanted_spec.samples=SDL_AUDIO_BUFFER_SIZE(1024)
 --------------------------*/
void audio_callback(void* userdata, Uint8* stream, int len) {
	VideoState* is = (VideoState*)userdata;//�����û�����
	int wt_stream_len, audio_size;//ÿ���������������ݳ��ȣ����������ݳ���
	double pts;//��Ƶʱ���

	while (len > 0) {//�����Ƶ�����ʣ�೤��
		if (is->audio_buf_index >= is->audio_buf_size) {//����Ƿ���Ҫִ�н������
			// We have already sent all our data; get more���ӻ����������ȡ���ݰ������룬�����ؽ��������ݳ��ȣ�audio_buf�����п��ܰ�����֡��������Ƶ����
			audio_size = audio_decode_frame(is, &pts);
			if (audio_size < 0) {//����������Ƿ�ɹ�
				// If error, output silence.
				is->audio_buf_size = 1024;
				memset(is->audio_buf, 0, is->audio_buf_size);//ȫ�����û�����
			}
			else {//�ڻص�������������Ƶͬ�����̣�������Ƶ���ݻ�����ж�֡(���ֵ)�����𵽽�����Ƶʱ������ͬ��Դʱ���Ŀ��
				audio_size = synchronize_audio(is, (int16_t*)is->audio_buf, audio_size, pts);//������Ƶͬ����Ļ��泤��
				is->audio_buf_size = audio_size;//����packet�а�����ԭʼ��Ƶ���ݳ���(��֡)
			}
			is->audio_buf_index = 0;//��ʼ���ۼ�д�뻺�泤��
		}

		wt_stream_len = is->audio_buf_size - is->audio_buf_index;//������뻺��ʣ�೤��
		if (wt_stream_len > len) {//���ÿ��д�뻺������ݳ����Ƿ񳬹�ָ������(1024)
			wt_stream_len = len;//ָ�����ȴӽ���Ļ�����ȡ����
		}

		//ÿ�δӽ���Ļ�����������ָ�����ȳ�ȡ���ݲ���������
		memcpy(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, wt_stream_len);
		len -= wt_stream_len;//���½�����Ƶ�����ʣ�೤��
		stream += wt_stream_len;//���»���д��λ��
		is->audio_buf_index += wt_stream_len;//�����ۼ�д�뻺�����ݳ���
	}
}

//��Ƶ(ͼ��)֡��Ⱦ
void video_display(VideoState* is) {
	SDL_Rect rect;//SDL���ζ���
	VideoPicture* vp;//ͼ��֡�ṹ��ָ��
	float aspect_ratio;//���/�߶ȱ�
	int w, h, x, y;//���ڳߴ缰��ʼλ��

	vp = &is->pictq[is->pictq_rindex];//��ͼ��֡����(����)����ȡͼ��֡�ṹ����
	if (vp->bmp) {//�����������ָ���Ƿ���Ч
		if (is->video_st->codec->sample_aspect_ratio.num == 0) {
			aspect_ratio = 0;
		}
		else {
			aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio) * is->video_st->codec->width / is->video_st->codec->height;
		}
		if (aspect_ratio <= 0.0) {
			aspect_ratio = (float)is->video_st->codec->width / (float)is->video_st->codec->height;
		}
		h = screen->h;
		w = ((int)rint(h * aspect_ratio)) & -3;
		if (w > screen->w) {
			w = screen->w;
			h = ((int)rint(w / aspect_ratio)) & -3;
		}
		x = (screen->w - w) / 2;
		y = (screen->h - h) / 2;

		//���þ�����ʾ����
		rect.x = x;
		rect.y = y;
		rect.w = w;
		rect.h = h;
		SDL_DisplayYUVOverlay(vp->bmp, &rect);//ͼ����Ⱦ
	}
}

//����/����ͼ��֡��Ϊͼ��֡�����ڴ�ռ�
void alloc_picture(void* userdata) {
	VideoState* is = (VideoState*)userdata;//�����û�����
	VideoPicture* vp = &is->pictq[is->pictq_windex];//��ͼ��֡����(����)����ȡͼ��֡�ṹ����

	if (vp->bmp) {//���ͼ��֡�Ƿ��Ѵ���
		// We already have one make another, bigger/smaller.
		SDL_FreeYUVOverlay(vp->bmp);//�ͷŵ�ǰoverlay����
	}
	// Allocate a place to put our YUV image on that screen������ָ���ߴ缰���ظ�ʽ���´������ػ�����
	vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width, is->video_st->codec->height, SDL_YV12_OVERLAY, screen);
	vp->width = is->video_st->codec->width;//����ͼ��֡���
	vp->height = is->video_st->codec->height;//����ͼ��֡�߶�

	SDL_LockMutex(is->pictq_lock);//������������������������������
	vp->allocated = 1;//ͼ��֡���ػ������ѷ����ڴ�
	SDL_CondSignal(is->pictq_ready);//���ȴ��̷߳�����Ϣ��֪ͨ�����Ѿ���
	SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����
}

/*---------------------------
 * queue_picture��ͼ��֡������еȴ���Ⱦ
 * @is��ȫ��״̬������
 * @pFrame������ͼ��������ݵĽṹ��
 * @pts����ǰͼ��֡�ľ�����ʾʱ���
 * 1�����ȼ��ͼ��֡����(����)�Ƿ���ڿռ�����µ�ͼ����û���㹻�Ŀռ����ͼ����ʹ��ǰ�߳����ߵȴ�
 * 2���ڳ�ʼ���������£�����(����)��VideoPicture��bmp����(YUV overlay)��δ����ռ䣬ͨ��FF_ALLOC_EVENT�¼��ķ�������alloc_picture����ռ�
 * 3��������(����)������VideoPicture��bmp����(YUV overlay)���ѷ���ռ������£�ֱ����������2��bmp���󿽱��������ݣ����������ڽ��и�ʽת����ִ�п�������
 ---------------------------*/
int queue_picture(VideoState* is, AVFrame* pFrame, double pts) {
	/*-------1���������Ƿ��в���ռ�------*/

		// Wait until we have space for a new pic.
	SDL_LockMutex(is->pictq_lock);//����������������ͼ��֡����
	while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {//�����е�ǰ����
		SDL_CondWait(is->pictq_ready, is->pictq_lock);//�߳����ߵȴ�pictq_ready�ź�
	}
	SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����

	if (is->quit) {//�������˳���ʶ
		return -1;
	}

	/*------2����ʼ��/����YUV overlay------*/
		// windex is set to 0 initially.
	VideoPicture* vp = &is->pictq[is->pictq_windex];//��ͼ��֡�����г�ȡͼ��֡����

	// Allocate or resize the buffer�����YUV overlay�Ƿ��Ѵ��ڣ������ʼ��YUV overlay���������ػ���ռ�
	if (!vp->bmp || vp->width != is->video_st->codec->width || vp->height != is->video_st->codec->height) {
		SDL_Event event;//SDL�¼�����

		vp->allocated = 0;//ͼ��֡δ����ռ�
		// We have to do it in the main thread.
		event.type = FF_ALLOC_EVENT;//ָ������ͼ��֡�ڴ��¼�
		event.user.data1 = is;//�����û�����
		SDL_PushEvent(&event);//����SDL�¼�

		// wait until we have a picture allocated.
		SDL_LockMutex(is->pictq_lock);//����������������ͼ��֡����
		while (!vp->allocated && !is->quit) {//��鵱ǰͼ��֡�Ƿ��ѳ�ʼ��(ΪSDL overlay)
			SDL_CondWait(is->pictq_ready, is->pictq_lock);//�߳����ߵȴ�alloc_picture����pictq_ready�źŻ��ѵ�ǰ�߳�
		}
		SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����
		if (is->quit) {//�������˳���ʶ
			return -1;
		}
	}

	/*-------3��������Ƶ֡��YUV overlay------*/
		// We have a place to put our picture on the queue.
		// If we are skipping a frame, do we set this to null but still return vp->allocated = 1?	
	AVFrame pict;//��ʱ����ת�����ͼ��֡���أ�������е�Ԫ�������
	if (vp->bmp) {//�����������ָ���Ƿ���Ч
		SDL_LockYUVOverlay(vp->bmp);//locks the overlay for direct access to pixel data��ԭ�Ӳ������������ػ�����������Ƿ��޸�

		// Point pict at the queue.
		pict.data[0] = vp->bmp->pixels[0];//��ת����ͼ���뻭�������ػ���������
		pict.data[1] = vp->bmp->pixels[2];
		pict.data[2] = vp->bmp->pixels[1];

		pict.linesize[0] = vp->bmp->pitches[0];//��ת����ͼ��ɨ���г����뻭�����ػ�������ɨ���г��������
		pict.linesize[1] = vp->bmp->pitches[2];//linesize-Size, in bytes, of the data for each picture/channel plane
		pict.linesize[2] = vp->bmp->pitches[1];//For audio, only linesize[0] may be set

		// Convert the image into YUV format that SDL uses����������ͼ��֡ת��ΪAV_PIX_FMT_YUV420P��ʽ����������ͼ��֡����
		sws_scale(is->sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, is->video_st->codec->height, pict.data, pict.linesize);

		SDL_UnlockYUVOverlay(vp->bmp);//Unlocks a previously locked overlay. An overlay must be unlocked before it can be displayed
		vp->pts = pts;//���ݵ�ǰͼ��֡�ľ�����ʾʱ���

		// Now we inform our display thread that we have a pic ready.
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {//���²���鵱ǰͼ��֡����д��λ��
			is->pictq_windex = 0;//����ͼ��֡����д��λ��
		}
		SDL_LockMutex(is->pictq_lock);//�������ж�д����������������
		is->pictq_size++;//����ͼ��֡���г���
		SDL_UnlockMutex(is->pictq_lock);//�ͷŶ��ж�д��
	}
	return 0;
}

//�޸�FFmpeg�ڲ��˳��ص���Ӧ�ĺ���
int decode_interrupt_cb(void* opaque) {
	return (global_video_state && global_video_state->quit);
}
int stream_component_open(VideoState* is, int stream_index);
/*---------------------------
 * parse_thread���������ݰ������̺߳���(����Ƶ�ļ��н���������Ƶ�������ݵ�Ԫ��һ��AVPacket��dataͨ����Ӧһ��NAL)
 * 1��ֱ��ʶ���ļ���ʽ�ͼ��ʶ��ý���ʽ
 * 2���򿪽����������������߳�
 * 3����������Ƶý������ҽӵ���Ӧ����
 ---------------------------*/
int parse_thread(void* arg) {
	VideoState* is = (VideoState*)arg;//�����û�����
	global_video_state = is;//����ȫ��״̬�����ṹ��
	AVFormatContext* pFormatCtx = NULL;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVPacket pkt, * packet = &pkt;//��ջ�ϴ�����ʱ���ݰ����󲢹���ָ��

	// Find the first video/audio stream.
	is->videoStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	is->audioStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1s
	int video_index = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	int audio_index = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	int i;//ѭ������

	AVDictionary* io_dict = NULL;
	AVIOInterruptCB callback;

	// Will interrupt blocking functions if we quit!.
	callback.callback = decode_interrupt_cb;
	callback.opaque = is;

	if (avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict)) {
		fprintf(stderr, "Unable to open I/O for %s\n", is->filename);
		return -1;
	}

	// Open video file������Ƶ�ļ���ȡ���ļ������ķ�װ��Ϣ����������
	if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0)
		return -1; // Couldn't open file.

	is->pFormatCtx = pFormatCtx;//�����ļ�������װ��Ϣ����������

	// Retrieve stream information��ȡ���ļ��б����������Ϣ
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		return -1; // Couldn't find stream information.
	}

	// Dump information about file onto standard error����ӡpFormatCtx�е�������Ϣ
	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// Find the first video stream.
	for (i = 0; i < pFormatCtx->nb_streams; i++) {//�����ļ��а�����������ý������(��Ƶ������Ƶ������Ļ����)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0) {//���ļ��а�������Ƶ��
			video_index = i;//����Ƶ�����͵ı���޸ı�ʶ��ʹ֮��Ϊ-1
		}
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0) {//���ļ��а�������Ƶ��
			audio_index = i;//����Ƶ�����͵ı���޸ı�ʶ��ʹ֮��Ϊ-1
		}
	}
	if (audio_index >= 0) {//����ļ����Ƿ������Ƶ��
		stream_component_open(is, audio_index);//����ָ�����ʹ���Ƶ��
	}
	if (video_index >= 0) {//����ļ����Ƿ������Ƶ��
		stream_component_open(is, video_index);//����ָ�����ʹ���Ƶ��
	}

	if (is->videoStream < 0 || is->audioStream < 0) {//����ļ����Ƿ��������Ƶ��
		fprintf(stderr, "%s: could not open codecs\n", is->filename);
		goto fail;
	}

	// Main decode loop.
	for (;;) {
		if (is->quit) {//����˳����̱�ʶ
			break;
		}
		// Seek stuff goes here���������Ƶ�������ݰ����г����Ƿ����
		if (is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE) {
			SDL_Delay(10);
			continue;
		}
		//���ļ������ζ�ȡÿ��ͼ��������ݰ������洢��AVPacket���ݽṹ��
		if (av_read_frame(is->pFormatCtx, packet) < 0) {
			if (is->pFormatCtx->pb->error == 0) {
				SDL_Delay(100); // No error; wait for user input.
				continue;
			}
			else {
				break;
			}
		}
		// Is this a packet from the video stream?.
		if (packet->stream_index == is->videoStream) {//������ݰ��Ƿ�Ϊ��Ƶ����
			packet_queue_put(&is->videoq, packet);//������в������ݰ�
		}
		else if (packet->stream_index == is->audioStream) {//������ݰ��Ƿ�Ϊ��Ƶ����
			packet_queue_put(&is->audioq, packet);//������в������ݰ�
		}
		else {//������ݰ��Ƿ�Ϊ��Ļ����
			av_packet_unref(packet);//�ͷ�packet�б����(��Ļ)��������
		}
	}
	// All done - wait for it.
	while (!is->quit) {
		SDL_Delay(100);
	}

fail:
	{
		SDL_Event event;//SDL�¼�����
		event.type = FF_QUIT_EVENT;//ָ���˳��¼�����
		event.user.data1 = is;//�����û�����
		SDL_PushEvent(&event);//�����¼�����ѹ��SDL��̨�¼�����
	}
	return 0;
}

/*-----------ȡ����Ƶʱ��-----------
 * ��ȡ�õ�ǰ������Ƶ���ݵ�pts������Ƶʱ����Ϊ����Ƶͬ����׼
 * ����Ƶͬ����ԭ���Ǹ�����Ƶ��pts��������Ƶ�Ĳ���
 * Ҳ����˵����Ƶ����һ֡���Ƿ���ʾ�Լ���ʾ�೤ʱ�䣬��ͨ����֡��PTS��ͬʱ���ڲ��ŵ���Ƶ��PTS�Ƚ϶�����
 * �����Ƶ��PTS�ϴ�����Ƶ׼���������ˢ�£�����ȴ�
 *
 * ��Ϊpcm���ݲ���audio_callback�ص���ʽ���в���
 * ������Ƶ��������ֻ�ܵõ�д��ص�����ǰ������Ƶ֡��pts�����޷��õ���ǰ����֡��pts(��Ҫ���õ�ǰ������Ƶ֡��pts��Ϊ�ο�ʱ��)
 * ���ǵ���Ƶ�Ĵ�С�벥��ʱ�������(��ͬ������)����ô��ǰʱ�����ڲ��ŵ���Ƶ֡pts(λ�ڻص�����������)
 * �Ϳ��Ը���������������pcm���ݳ��ȡ�������ʣ��pcm���ݳ��ȣ����泤�ȼ������ʽ���������
 --------------------------------*/
double get_audio_clock(VideoState* is) {
	double pts = is->audio_clock;//Maintained in the audio thread��ȡ�ý���������ʱ�ĵ�ǰ����ʱ���
	//��δ(��������)���ŵ�ʣ��ԭʼ��Ƶ���ݳ��ȣ����ڽ����Ķ�֡ԭʼ��Ƶ���ݳ���-�ۼ�д����������ĳ���
	int hw_buf_size = is->audio_buf_size - is->audio_buf_index;
	int bytes_per_sec = 0;//��ʼ��ÿ���ԭʼ��Ƶ�ֽ���
	int pcm_bytes = is->audio_st->codec->channels * 2;//ÿ��ԭʼ��Ƶ�����ֽ���=������*ÿ���������ֽ���
	if (is->audio_st) {
		bytes_per_sec = is->audio_st->codec->sample_rate * pcm_bytes;//����ÿ���ԭʼ��Ƶ�ֽ���
	}
	if (bytes_per_sec) {//���ÿ���ԭʼ��Ƶ�ֽ����Ƿ���Ч
		pts -= (double)hw_buf_size / bytes_per_sec;//����д���������������λ�ã���ǰ���Ƽ��㵱ǰʱ�̵���Ƶ����ʱ���pts
	}
	return pts;//���ص�ǰ���ڲ��ŵ���Ƶʱ���
}

/*-----------ȡ����Ƶʱ��-----------
 * ��ȡ�õ�ǰ������Ƶ֡��pts������Ƶʱ��pts��Ϊ����Ƶͬ����׼��return the current time offset of the video currently being played
 * ��ֵΪ��ǰ֡ʱ���pts+һ��΢С������ֵdelta
 * ��Ϊ��ms�ļ����ϣ��ں��뼶���ϣ���ȡ����Ƶʱ��(����ǰ֡pts)��ʱ�̣��������Ƶʱ�ӵ�ʱ��(�罫��Ƶͬ��������Ƶptsʱ��)�����ӳ�
 * ��ô����Ƶʱ����Ҫ�ڱ�����ʱ��������������ֵdeltaΪ
 * delta=[ȡ����Ƶʱ�ӵ�ʱ��ֵvideo_current_pts_time] �� [����get_video_clockʱ��ֵ] �ļ��ʱ��
 * ͨ������£�����ѡ�����ⲿʱ�ӻ���Ƶʱ����Ϊ��ͬ��Դ������Ƶͬ������Ƶ���ⲿʱ��Ϊ��ѡͬ������
 * ����Ƶʱ����Ϊ��ͬ��Դ��ͬ������������3�ֻ�����ͬ������(ͬ������Ƶ��ͬ������Ƶ��ͬ�����ⲿʱ��)
 * ������Ϊչʾͬ������Ƶʱ�ӵķ�����һ�������ͬ������Ƶʱ�ӽ���Ϊ������ͬ������
 --------------------------------*/
double get_video_clock(VideoState* is) {
	double delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
	//pts_of_last_frame+(Current_time-time_elapsed_since_pts_value_was_set)
	return is->video_current_pts + delta;
}

//ȡ��ϵͳʱ�䣬��ϵͳʱ����Ϊͬ����׼
double get_external_clock(VideoState* is) {
	return av_gettime() / 1000000.0;//ȡ��ϵͳ��ǰʱ�䣬��1/1000000��Ϊ��λ�������ڸ���ƽ̨��ֲ
}

//ȡ����ʱ��(��׼ʱ��)
double get_master_clock(VideoState* is) {
	if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
		return get_video_clock(is);//������Ƶʱ��
	}
	else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
		return get_audio_clock(is);//������Ƶʱ��
	}
	else {
		return get_external_clock(is);//����ϵͳʱ��
	}
}

/*---------------------------
* return the wanted number of samples to get better sync if sync_type is video or external master clock
* ͨ������»�����Ƶ��ϵͳʱ��Ϊ��ͬ��Դ��ֻ������Ƶ��ϵͳʱ��ʧЧ������²�����ƵΪ��ͬ��Դ
* �ú����ȶ���Ƶʱ������ͬ��Դ��ʱ�ͨ����̬��֡(���ֵ)������Ƶ���ݣ����𵽼���(������)��Ƶ����ʱ������������ͬ��Դʱ�������
* �ú�������Ƶ�������ݽ��ж�֡(���ֵ)�����ض�֡(���ֵ)�����Ƶ���ݳ���
* ��Ϊ��Ƶͬ�����ܴ�����������������ȸ����ã��ú���ͨ����Ƶ��ͬ������(audio_diff_avg_count)��ʱ���ֵ(avg_diff)��Լ����Ƶ��ͬ������
---------------------------*/
int synchronize_audio(VideoState* is, short* samples, int samples_size, double pts) {
	double ref_clock;//��ͬ��Դ(��׼ʱ��)
	int pcm_bytes = is->audio_st->codec->channels * 2;//ÿ����Ƶ�����ֽ���=������*ÿ���������ֽ���
	/* if not master, then we try to remove or add samples to correct the clock */
	if (is->av_sync_type != AV_SYNC_AUDIO_MASTER) {//�����ͬ��Դ����ͬ��Դ������Ƶʱ�ӵ�����£�ִ�����´���
		double diff, avg_diff;//diff-��Ƶ֡���ż�����ͬ��Դʱ�avg_diff-������ͬ��ƽ��ֵ
		int wanted_size, min_size, max_size;//������֡(���ֵ)��Ļ��泤�ȣ����泤�����/��Сֵ

		ref_clock = get_master_clock(is);//ȡ�õ�ǰ��ͬ��Դ������ͬ��ԴΪ��׼ʱ��
		diff = get_audio_clock(is) - ref_clock;//������Ƶʱ���뵱ǰ��ͬ��Դ��ʱ��

		if (diff < AV_NOSYNC_THRESHOLD) {//�����Ƶ�Ƿ��ڲ�ͬ��״̬(ͨ��AV_NOSYNC_THRESHOLD���ƶ�������Ƶ���ݳ��ȣ������������������)
			//Accumulate the diffs����ʱ���Ȩ�ۼ�(�뵱ǰ����ʱ�����ʱ��Ȩֵϵ����)
			is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
			if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {//����Ƶ��ͬ����������ֵ���бȶ�
				//not enough measures to have a correct estimate
				is->audio_diff_avg_count++;//��Ƶ��ͬ����������
			}
			else {//����Ƶ��ͬ������������ֵ�޶��󣬴�����Ƶͬ������
				avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);//����ʱ���ֵ(�ȱȼ�������ƽ����)
				if (fabs(avg_diff) >= is->audio_diff_threshold) {//�ȶ�ʱ���ֵ��ʱ����ֵ
					wanted_size = samples_size + ((int)(diff * is->audio_st->codec->sample_rate) * pcm_bytes);//����ʱ���ͬ����Ļ��泤��
					min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);//ͬ����Ļ��泤����Сֵ
					max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);//ͬ����Ļ��泤�����ֵ
					if (wanted_size < min_size) {//��ͬ���󻺴泤��<��С���泤��
						wanted_size = min_size;//����С���泤����Ϊͬ����Ļ��泤��
					}
					else if (wanted_size > max_size) {//��ͬ���󻺴泤��>��С���泤��
						wanted_size = max_size;//����󻺴泤����Ϊͬ����Ļ��泤��
					}
					if (wanted_size < samples_size) {//�ȶ�ͬ�������Ƶ�������ݳ�����ԭʼ���泤��
						samples_size = wanted_size;//Remove samples���ö�֡�����Ƶ���泤�ȸ���ԭʼ���泤��
					}
					else if (wanted_size > samples_size) {//��ͬ���󻺴泤�ȴ��ڵ�ǰ���泤��
					 //Add samples by copying final sample��ͨ���������һ����Ƶ���ݽ��в�ֵ
					 //int nb=samples_size-wanted_size;
						int nb = wanted_size - samples_size;//�����ֵ�󻺴泤����ԭʼ���泤�ȼ�Ĳ�ֵ(��Ҫ��ֵ����Ƶ��������)
						uint8_t* samples_end = (uint8_t*)samples + samples_size - pcm_bytes;//ȡ�û���ĩ������ָ��
						uint8_t* q = samples_end + pcm_bytes;//��ʼ��ֵλ��|<-----samples----->||q|
						while (nb > 0) {//����ֵ��Ƶ����(ÿ���������������pcm����)
							memcpy(q, samples_end, pcm_bytes);//��samplesԭʼ�����׷�Ӳ�ֵ
							q += pcm_bytes;//���²�ֵλ��
							nb -= pcm_bytes;//���²�ֵ����
						}
						samples_size = wanted_size;//������Ƶͬ����Ļ��泤��
					}
				}
			}
		}
		else {
			// Difference is too big, reset diff stuff��ʱ���������ʱ���ۼ�ֵ
			is->audio_diff_avg_count = 0;//��Ƶ��ͬ����������
			is->audio_diff_cum = 0;//��Ƶ�ۼ�ʱ������
		}
	}//end for if (is->av_sync_type != AV_SYNC_AUDIO_MASTER)
	return samples_size;//���ط��͵���������Ƶ�����ֽ���
}

//��ʱ�������Ļص�����
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque) {
	SDL_Event event;//SDL�¼�����
	event.type = FF_REFRESH_EVENT;//��Ƶ��ʾˢ���¼�
	event.user.data1 = opaque;//�����û�����
	SDL_PushEvent(&event);//�����¼�
	return 0;//0 means stop timer
}

/*---------------------------
 * Schedule a video refresh in 'delay' ms.
 * ����һ��֡���ŵ��ӳ�
 * ����ϵͳ��ָ������ʱ��������һ��FF_REFRESH_EVENT�¼����������ڽ�����������
 * ����¼������¼������ﴥ��sdl_refresh_timer_cb�����ĵ���
 * @delay������ͼ��֡�Ľ���˳������Ⱦ˳��һ�µ�����£�������һ֡����Ⱦʱ��
 * �Ӷ������ܵ�ʹ����ͼ��֡���չ̶���֡����Ⱦˢ��
 --------------------------*/
static void schedule_refresh(VideoState* is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);//��ָ����ʱ��(ms)��ص��û�ָ���ĺ���
}

/*---------------------------
 * ��ʾˢ�º���(FF_REFRESH_EVENT��Ӧ����)
 * ����Ƶͬ������ʱ���ϣ�������һ֡���ӳ�ʱ��
 * ʹ�õ�ǰ֡��PTS����һ֡��PTS�������Ʋ�����һ֡���ӳ�ʱ�䣬������video�Ĳ����ٶ�����������ӳ�ʱ��
 ---------------------------*/
void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;//�����û�����
	VideoPicture* vp;//ͼ��֡����
	//delay-ǰ����֡��ʾʱ����([����-����]ʱ��)��diff-ͼ��֡��ʾ����Ƶ֡���ż��ʱ���
	//sync_threshold-[����-����]��Сʱ��actual_delay-��ǰ֡-����֡����ʾʱ����(��̬ʱ�䡢��ʵʱ�䡢����ʱ��)
	double delay, diff, sync_threshold, actual_delay, ref_clock;

	if (is->video_st) {//���ȫ��״̬�������е���Ƶ����Ϣ�ṹ���Ƿ���Ч(�Ƿ��Ѽ�����Ƶ�ļ�)
		if (is->pictq_size == 0) {//���ͼ��֡�������Ƿ��еȴ���ʾˢ�µ�ͼ��
			schedule_refresh(is, 1);//������Ϊ�գ�������ʾˢ���¼����ٴν���video_refresh_timer����
		}
		else {
			vp = &is->pictq[is->pictq_rindex];//����ʾ������ȡ�õȴ���ʾ��ͼ��֡

			is->video_current_pts = vp->pts;//ȡ�õ�ǰ֡����ʾʱ���
			is->video_current_pts_time = av_gettime();//ȡ��ϵͳʱ�䣬��Ϊ��ǰ֡���ŵ�ʱ���׼
			//���㵱ǰ֡��ǰһ֡��ʾ(pts)�ļ��ʱ��(��ʾʱ����Ĳ�ֵ)
			delay = vp->pts - is->frame_last_pts;//The pts from last time��[����-����]ʱ���
			if (delay <= 0 || delay >= 1.0) {//���ʱ�����Ƿ��ں���Χ
				// If incorrect delay, use previous one.
				delay = is->frame_last_delay;//����֮ǰ�Ķ�̬ˢ�¼��ʱ��
			}
			// Save for next time.
			is->frame_last_delay = delay;//����(����)[��һ֡ͼ��]�Ķ�̬ˢ���ӳ�ʱ��
			is->frame_last_pts = vp->pts;//����(����)[��һ֡ͼ��]����ʾʱ���

			// Update delay to sync to audio��ȡ����������ʱ���(��Ϊ��Ƶͬ���Ĳο�ʱ��)
			if (is->av_sync_type != AV_SYNC_VIDEO_MASTER) {//�����ͬ��ʱ��Դ
				ref_clock = get_master_clock(is);//������ʱ�����ж�Video���ŵĿ���������ʱ��Ϊ��׼ʱ��
				diff = vp->pts - ref_clock;//����ͼ��֡��ʾ����ʱ�ӵ�ʱ���
				//����ʱ������������һ֡���ӳ�ʱ�䣬��ʵ��ͬ�� Skip or repeat the frame��Take delay into account
				sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
				//�ж�����Ƶ��ͬ����������[����-����]ʱ���&[����-����]ʱ���<10ms��ֵ����>����ֵ��Ϊ���ģʽ������������Ƶͬ������
				if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
					if (diff <= -sync_threshold) {//���ˣ�delay��Ϊ0������ʾ
						//��һ֡������ʾ��ʱ��͵�ǰ�������ܽ��Ļ��ӿ���ʾ��һ֡��������video_display��ʾ�굱ǰ֡������ʱ���ܿ�ȥ��ʾ��һ֡
						delay = 0;
					}
					else if (diff >= sync_threshold) {//���ˣ��ӱ�delay
						delay = 2 * delay;
					}
				}//���diff(����)����AV_NOSYNC_THRESHOLD���������ģʽ�ˣ���������̫�󣬲���������Ƶͬ����������
			}
			//������Ƶ���ŵ���ǰ֡ʱ���Ѳ���ʱ��ֵ(����ͼ��֡��̬�����ۼ�ʱ��ֵ-��ʵֵ)��frame_timerһֱ�ۼ��ڲ��Ź��������Ǽ������ʱ
			is->frame_timer += delay;
			//ÿ�μ���frame_timer��ϵͳʱ��Ĳ�ֵ(��ϵͳʱ��Ϊ��׼ʱ��)����frame_timer��ϵͳʱ��(����ʱ��)�������Ŀ��
			actual_delay = is->frame_timer - (av_gettime() / 1000000.0);//Computer the REAL delay
			if (actual_delay < 0.010) {//������ʱ�䷶Χ
				actual_delay = 0.010;// Really it should skip the picture instead
			}
			schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));//�þ���ʱ�俪��ʱ��ȥ��̬��ʾˢ����һ֡
			video_display(is);//ˢ��ͼ��Show the picture

			// update queue for next picture!.
			if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {//����ͼ��֡���ж�����λ��
				is->pictq_rindex = 0;//���������ִ����β�������ö�����λ��
			}
			SDL_LockMutex(is->pictq_lock);//������������������������������
			is->pictq_size--;//����ͼ��֡���г���
			SDL_CondSignal(is->pictq_ready);//���Ͷ��о����ź�
			SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����
		}
	}
	else {//����Ƶ��Ϣ��ȡʧ�ܣ��򾭹�ָ����ʱ(100ms)�����³���ˢ����ͼ
		schedule_refresh(is, 100);
	}
}

/*---------------------------
 * ��鵱ǰ֡����ʾʱ���pts�������ڲ���Ƶ���ż�ʱ��(��¼��Ƶ�Ѿ���ʱ��(video_clock)��
 * @is��ȫ��״̬������
 * @src_frame����ǰ(�����)(�����µ�)ͼ��֡����
 * @pts����ǰͼ��֡����ʾʱ���
 ---------------------------*/
double synchronize_video(VideoState* is, AVFrame* src_frame, double pts) {
	/*----------�����ʾʱ���----------*/
	if (pts != 0) {//�����ʾʱ����Ƿ���Ч
		// If we have pts, set video clock to it.
		is->video_clock = pts;//����ʾʱ��������Ѳ���ʱ��
	}
	else {//����ȡ������ʾʱ���
	 // If we aren't given a pts, set it to the clock.
		pts = is->video_clock;//���Ѳ���ʱ�������ʾʱ���
	}
	/*--------������Ƶ�Ѿ���ʱ��--------*/
		// Update the video clock������֡Ҫ�ظ���ʾ����ȫ����Ƶ����ʱ��video_clockӦ�����ظ���ʾ������*֡��
	double frame_delay = av_q2d(is->video_st->codec->time_base);//��֡��ʾ�꽫Ҫ���ѵ�ʱ��
	// If we are repeating a frame, adjust clock accordingly,�������ظ�֡�������������ŵ�ǰ����֡ͼ��䰲����Ⱦ�ظ�֡
	frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);//������Ⱦ�ظ�֡��ʱֵ(����������ʱֵ)
	is->video_clock += frame_delay;//������Ƶ����ʱ��
	return pts;//��ʱ���ص�ֵ��Ϊ��һ֡��Ҫ��ʼ��ʾ��ʱ���
}

// These are called whenever we allocate a frame buffer. We use this to store the global_pts in a frame at the time it is allocated.
int our_get_buffer(struct AVCodecContext* c, AVFrame* pic, int flags) {
	int ret = avcodec_default_get_buffer2(c, pic, 0);
	uint64_t* pts = (uint64_t*)av_malloc(sizeof(uint64_t));
	*pts = global_video_pkt_pts;
	pic->opaque = pts;
	return ret;
}

//��Ƶ�����̺߳�������Ҫ�����ӷ������֡�����SDL��ʾ��������ѭ������(�Ӷ�����ȡ����֡�����룬����ʱ�ӣ���ʾ)���ͷ���Ƶ����֡/���ݰ�����
int decode_thread(void* arg) {
	VideoState* is = (VideoState*)arg;//�����û�����
	AVPacket pkt, * packet = &pkt;//��ջ�ϴ�����ʱ���ݰ����󲢹���ָ��
	int frameFinished;//��������Ƿ�ɹ���ʶ������֡������־frameFinished
	// Allocate video frame��Ϊ��������Ƶ��Ϣ�ṹ�����ռ䲢��ɳ�ʼ������(�ṹ���е�ͼ�񻺴水�����������ֶ���װ)
	AVFrame* pFrame = av_frame_alloc();
	double pts;//��ǰ����������Ƶ�е�(����)ʱ��λ��

	for (;;) {
		if (packet_queue_get(&is->videoq, packet, 1) < 0) {//�Ӷ�������ȡ���ݰ���packet��������ȡ�����ݰ�������
			// Means we quit getting packets.
			break;
		}
		pts = 0;//(����)��ʾʱ�����ʼ��

		// Save global pts to be stored in pFrame in first call.
		global_video_pkt_pts = packet->pts;
		/*-------------------------
		 * Decode video frame������������һ֡���ݣ�����frameFinished����Ϊtrue
		 * �����޷�ͨ��ֻ����һ��packet�ͻ��һ����������Ƶ֡frame��������Ҫ��ȡ���packet���У�avcodec_decode_video2()���ڽ��뵽������һ֡ʱ����frameFinishedΪ��
		 * avcodec_decode_video2�ᰴ��dtsָ����˳����룬Ȼ������ȷ����ʾ˳�����ͼ��֡��������ͼ��֡��pts
		 * ������˳������ʾ˳����ڲ�һ�£���avcodec_decode_video2���ȶԵ�ǰ֡���л���(��ʱframeFinished=0)��Ȼ������ȷ��˳�����ͼ��֡
		 * The decoder bufferers a few frames for multithreaded efficiency
		 * It is also absolutely required to delay decoding in the case of B frames
		 * where the decode order may not be the same as the display order.
		 * Takes input raw video data from frame and writes the next output packet,
		 * if available, to avpkt. The output packet does not necessarily contain data for the most recent frame,
		 * as encoders can delay and reorder input frames internally as needed.
		 -------------------------*/
		avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished, packet);

		//ȡ�ñ������ݰ��е���ʾʱ���PTS(int64_t),����ʱ������pts(double)��
//		if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque && *(uint64_t*)pFrame->opaque != AV_NOPTS_VALUE) {
//			pts = *(uint64_t *)pFrame->opaque;
//		} else if (packet->dts != AV_NOPTS_VALUE) {
//			pts = packet->dts;
//		} else {
//			pts = 0;
//		}
		pts = av_frame_get_best_effort_timestamp(pFrame);//ȡ�ñ������ݰ��е�ͼ��֡��ʾ���PTS(int64_t),����ʱ������pts(double)��
		/*-------------------------
		 * �ڽ����̺߳����м��㵱ǰͼ��֡����ʾʱ���
		 * 1��ȡ�ñ������ݰ��е�ͼ��֡��ʾ���PTS(int64_t),����ʱ������pts(double)��
		 * 2������PTS*time_base�����㵱ǰ����������Ƶ�е���ʾʱ�������PTS*(1/framerate)
		 *    av_q2d��AVRatioal�ṹת����double�ĺ�����
		 *    ���ڼ�����ƵԴÿ��ͼ��֡��ʾ�ļ��ʱ��(1/framerate),������(time_base->num/time_base->den)
		 -------------------------*/
		 //����pts=PTS*time_base={numerator=1,denominator=25}���㵱ǰ����������Ƶ�е���ʾʱ���
		pts *= av_q2d(is->video_st->time_base);//time_baseΪAVRational�������ṹ��{num=1,den=25}����¼����ƵԴÿ��ͼ��֡��ʾ�ļ��ʱ��

		// Did we get a video frame?.
		if (frameFinished) {
			pts = synchronize_video(is, pFrame, pts);//��鵱ǰ֡����ʾʱ���pts�������ڲ���Ƶ���ż�ʱ��(��¼��Ƶ�Ѿ���ʱ��(video_clock)��
			if (queue_picture(is, pFrame, pts) < 0) {//��������ɵ�ͼ��֡��ӵ�ͼ��֡����,����¼ͼ��֡�ľ�����ʾʱ���pts
				break;
			}
		}
		av_packet_unref(packet);//�ͷ�pkt�б���ı�������
	}
	av_free(pFrame);//���pFrame�е��ڴ�ռ�
	return 0;
}

//����ָ�����ʹ������ҵ���Ӧ�Ľ�������������Ӧ����Ƶ���á�����ؼ���Ϣ��VideoState��������Ƶ����Ƶ�����߳�
int stream_component_open(VideoState* is, int stream_index) {
	AVFormatContext* pFormatCtx = is->pFormatCtx;//�����ļ������ķ�װ��Ϣ����������
	AVCodecContext* codecCtx = NULL;//�����������Ķ��󣬽�������������ػ�����״̬����Դ�Լ��������Ľӿ�ָ��
	AVCodec* codec = NULL;//������������Ϣ�Ľṹ�壬�ṩ���������Ĺ����ӿڣ����Կ����Ǳ��������������һ��ȫ�ֱ���
	AVDictionary* optionsDict = NULL;//SDL_AudioSpec a structure that contains the audio output format������ SDL_AudioSpec �ṹ�壬������Ƶ��������
	SDL_AudioSpec wanted_spec, spec;//wanted_spec�ǽ�������SDL�����Ƶ������spec��SDL�ⷵ�ظ���������֧�ֵ���Ƶ������������󳬹�SDL֧�ַ��أ��򷵻����������

	//���������������Ƿ��ں���Χ��
	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
		return -1;
	}

	// Get a pointer to the codec context for the video stream.
	codecCtx = pFormatCtx->streams[stream_index]->codec;//ȡ�ý�����������

	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {//�������������Ƿ�Ϊ��Ƶ������
		// Set audio settings from codec info,SDL_AudioSpec a structure that contains the audio output format
		// ����SDL_AudioSpec�ṹ�壬������Ƶ�����������
		wanted_spec.freq = codecCtx->sample_rate;//����Ƶ�� DSP frequency -- samples per second
		wanted_spec.format = AUDIO_S16SYS;//������ʽ Audio data format
		wanted_spec.channels = codecCtx->channels;//������ Number of channels: 1 mono, 2 stereo
		wanted_spec.silence = 0;//�����ʱ�Ƿ���
		//Ĭ��ÿ�ζ���Ƶ����Ĵ�С���Ƽ�ֵΪ 512~8192��ffplayʹ�õ���1024 specifies a unit of audio data refers to the size of the audio buffer in sample frames
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;//���ö�ȡ��Ƶ���ݵĻص��ӿں��� the function to call when the audio device needs more data
		wanted_spec.userdata = is;//�����û�����

		//Opens the audio device with the desired parameters(wanted_spec)����ָ����������Ƶ�豸
		if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}
		is->audio_hw_buf_size = spec.size;
	}

	// Find the decoder for the video stream��������Ƶ����Ӧ�Ľ����������Ĳ��Ҷ�Ӧ�Ľ����������ض�Ӧ�Ľ�����(��Ϣ�ṹ��)
	codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec || (avcodec_open2(codecCtx, codec, &optionsDict) < 0)) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	//������������
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_AUDIO://��Ƶ������
		is->audioStream = stream_index;//��Ƶ�����ͱ�ų�ʼ��
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_buf_size = 0;//�����Ķ�֡��Ƶ���ݳ���
		is->audio_buf_index = 0;//�ۼ�д���������ݳ���

		// Averaging filter for audio sync.
		is->audio_diff_avg_coef = exp(log(0.01 / AUDIO_DIFF_AVG_NB));//��Ƶʱ������ͬ��Դ�ۼ�ʱ���Ȩϵ��
		is->audio_diff_avg_count = 0;//��Ƶ��ͬ��������ʼ��
		// Correct audio only if larger error than this.
		is->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / codecCtx->sample_rate;

		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);//��Ƶ���ݰ����г�ʼ��
		SDL_PauseAudio(0);//audio callback starts running again��������Ƶ�豸�������ʱ��û�л��������ô���;���
		break;
	case AVMEDIA_TYPE_VIDEO://��Ƶ������
		is->videoStream = stream_index;//��Ƶ�����ͱ�ų�ʼ��
		is->video_st = pFormatCtx->streams[stream_index];
		//��ϵͳʱ��Ϊ��׼����ʼ�����ŵ���ǰ֡���Ѳ���ʱ��ֵ����ֵΪ��ʵʱ��ֵ����̬ʱ��ֵ������ʱ��ֵ
		is->frame_timer = (double)av_gettime() / 1000000.0;
		is->frame_last_delay = 40e-3;//��ʼ����һ֡ͼ��Ķ�̬ˢ���ӳ�ʱ��
		is->video_current_pts_time = av_gettime();//ȡ��ϵͳ��ǰʱ��

		packet_queue_init(&is->videoq);//��Ƶ���ݰ����г�ʼ��
		is->video_tid = SDL_CreateThread(decode_thread, is);//���������߳�
		// Initialize SWS context for software scaling������ͼ��ת�����ظ�ʽΪAV_PIX_FMT_YUV420P
		is->sws_ctx = sws_getContext(is->video_st->codec->width, is->video_st->codec->height,
			is->video_st->codec->pix_fmt, is->video_st->codec->width,
			is->video_st->codec->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
			NULL, NULL, NULL);
		codecCtx->get_buffer2 = our_get_buffer;
		break;
	default:
		break;
	}
	return 0;
}

//��ں�������ʼ��SDL�⣬ע��SDL��Ϣ�¼��������ļ������̣߳�������Ϣѭ��
int main(int argc, char* argv[]) {
	//if (argc < 2) {//���������������Ƿ���ȷ
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}

	av_register_all();// Register all formats and codecs��ע�����ж�ý���ʽ���������
	VideoState* is = (VideoState*)av_mallocz(sizeof(VideoState));//����ȫ��״̬����
	av_strlcpy(is->filename, fileName, 1024);//������Ƶ�ļ�·����
	is->pictq_lock = SDL_CreateMutex();//�����������ݰ����л���������
	is->pictq_ready = SDL_CreateCond();//�����������ݰ����о�����������

	//SDL_Init initialize the Event Handling, File I/O, and Threading subsystems����ʼ��SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());//initialize the video audio & timer subsystem
		exit(1);
	}

	// Make a screen to put our video,��SDL2.0��SDL_SetVideoMode��SDL_Overlay�Ѿ����ã���ΪSDL_CreateWindow��SDL_CreateRenderer�������ڼ���ɫ��
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(640, 480, 0, 0);//����SDL���ڼ���ͼ���棬��ָ��ͼ��ߴ缰���ظ�ʽ
#else
	screen = SDL_SetVideoMode(640, 480, 24, 0);//����SDL���ڼ���ͼ���棬��ָ��ͼ��ߴ缰���ظ�ʽ
#endif
	if (!screen) {//���SDL(��ͼ����)�����Ƿ񴴽��ɹ�(SDL�û�ͼ��������������)
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}

	schedule_refresh(is, 40);//��ָ����ʱ��(40ms)��ص��û�ָ���ĺ���������ͼ��֡����ʾ����

	is->av_sync_type = DEFAULT_AV_SYNC_TYPE;//ָ����ͬ��Դ
	is->parse_tid = SDL_CreateThread(parse_thread, is);//�����������ݰ������߳�
	if (!is->parse_tid) {//����߳��Ƿ񴴽��ɹ�
		av_free(is);
		return -1;
	}

	/*-----------------------
	 * SDL�¼�(��Ϣ)ѭ��
	 * ������ͨ����Ϣѭ�����ƣ�����ѭ�������Ĵ���(����)ͼ��֡��Ⱦ���������������Ƶ�ļ�����Ⱦ
	 * �������������ڰ���ָ�����ĵ�����٣���Ϣѭ�����Ʊ�֤����Ƶ���չ̶��Ľ���(6/8)����
	 * ��Ϊ���ڽ���˳������Ⱦ˳��һ�µ����(����B֡�����)����Ƶͬ�����Ʊ�֤����ͼ���ڽ���󶼾������չ̶����Ĳ���
	 * ��ÿ�ε���Ϣ��Ӧ����video_refresh_timer�У����¼�����һ֡����ʾʱ��
	 * ��ͨ��schedule_refreshָ��ʱ��(�����ڽ���������)��������һ�ֵ�ͼ��֡��ʾ
	 -----------------------*/
	SDL_Event event;//SDL�¼�(��Ϣ)����
	for (;;) {
		SDL_WaitEvent(&event);//Use this function to wait indefinitely for the next available event�����߳��������ȴ��¼�����
		switch (event.type) {//�¼������������̺߳󣬼���¼����ͣ�ִ����Ӧ����
		case FF_QUIT_EVENT:
		case SDL_QUIT://�˳������¼�
			is->quit = 1;
			//If the video has finished playing, then both the picture and audio queues are waiting for more data.  
			//Make them stop waiting and terminate normally.
			SDL_CondSignal(is->audioq.qready);//�������о����źű�������
			SDL_CondSignal(is->videoq.qready);
			SDL_Quit();
			exit(0);
			break;
		case FF_ALLOC_EVENT://����overlay�¼�
			alloc_picture(event.user.data1);//����overlay�¼���Ӧ����
			break;
		case FF_REFRESH_EVENT://��Ƶ��ʾˢ���¼�
			video_refresh_timer(event.user.data1);//��Ƶ��ʾˢ���¼���Ӧ����
			break;
		default:
			break;
		}
	}
	return 0;
}

