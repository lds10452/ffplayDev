// tutorial04.c
// A pedagogical video player that will stream through every video frame as fast as it can,
// and play audio (out of sync).
//
// This tutorial was written by Stephen Dranger (dranger@gmail.com).
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
//
//
// Updates tested on:
// Mac OS X 10.11.6
// Apple LLVM version 8.0.0 (clang-800.0.38)
//
// Use 
//
// $ gcc -o tutorial04 tutorial04.c -lavutil -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
//
// to build (assuming libavutil/libavformat/libavcodec/libswscale are correctly installed your system).
//
// Run using
//
// $ tutorial04 myvideofile.mpg
//
// to play the video stream on your screen.

/*---------------------------
//1����Ϣ���д��������ڴ�����Ϣǰ���ȶԻ����������������Ա�����Ϣ�����е��ٽ�����Դ
//2������Ϣ����Ϊ�գ������pthread_cond_wait�Ի�������ʱ�������ȴ������߳�����Ϣ�����в�����Ϣ����
//3���������߳�����Ϣ�����в�����Ϣ���ݺ�ͨ��pthread_cond_signal��ȴ��̷߳���qready�ź�
//4����Ϣ���д����߳��յ�qready�źű����ѣ����»�ö���Ϣ�����ٽ�����Դ�Ķ�ռ

#include <pthread.h>

struct msg{//��Ϣ���нṹ��
	struct msg *m_next;//��Ϣ���к�̽ڵ�
	//more stuff here
}

struct msg *workq;//��Ϣ����ָ��
pthread_cond_t qready=PTHREAD_COND_INITIALIZER;//��Ϣ���о�����������
pthread_mutex_t qlock=PTHREAS_MUTEX_INITIALIZER;//��Ϣ���л�������������Ϣ��������

//��Ϣ���д�������
void process_msg(void){
	struct msg *mp;//��Ϣ�ṹָ��
	for(;;){
		pthread_mutex_lock(&qlock);//��Ϣ���л�����������������Ϣ��������
		while(workq==NULL){//�����Ϣ�����Ƿ�Ϊ�գ���Ϊ��
			pthread_cond_wait(&qready,&qlock);//�ȴ���Ϣ���о����ź�qready�����Ի�������ʱ�������ú�������ʱ���������ٴα���ס
		}
		mp=workq;//�߳�����������Ϣ������ȡ����׼������
		workq=mp->m_next;//������Ϣ���У�ָ��������ȡ������Ϣ
		pthread_mutex_unlock(&qlock);//�ͷ���
		//now process the message mp
	}
}

//����Ϣ������Ϣ����
void enqueue_msg(struct msg *mp){
	pthread_mutex_lock(&qlock);//��Ϣ���л�����������������Ϣ��������
	mp->m_next=workq;//��ԭ����ͷ��Ϊ������Ϣ�ĺ�̽ڵ�
	workq=mp;//������Ϣ�������
	pthread_cond_signal(&qready);//���ȴ��̷߳���qready��Ϣ��֪ͨ��Ϣ�����Ѿ���
	pthread_mutex_unlock(&qlock);//�ͷ���
}
---------------------------*/

#include "SDL.h"
extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
//	#include "libavutil/imgutils.h"
	#include "libavformat/avio.h"
	#include "libavutil/avstring.h"
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

#define FF_ALLOC_EVENT (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

#define VIDEO_PICTURE_QUEUE_SIZE 1

SDL_Surface* screen;//SDL��ͼ���棬A structure that contains a collection of pixels used in software blitting
SDL_mutex* screen_lock;//SDL������������ͼ��֡��Ⱦ��ʱ�򣬱�����ͼ������������ݲ��������޸�

/*-------�����ڵ�ṹ��--------
typedef struct AVPacketList {
	AVPacket pkt;//��������
	struct AVPacketList *next;//������̽ڵ�
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

//ͼ��֡�ṹ��
typedef struct VideoPicture {
	SDL_Overlay* bmp;//SDL����overlay
	int width, height;//Source height & width.
	int allocated;//�Ƿ�����ڴ�ռ䣬��Ƶ֡ת��ΪSDL overlay��ʶ
} VideoPicture;

typedef struct VideoState {
	AVFormatContext* pFormatCtx;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVPacket audio_pkt;//����Ӷ�������ȡ�����ݰ�
	AVFrame audio_frame;//��������ݰ��н������Ƶ����
	AVStream* video_st;//��Ƶ����Ϣ�ṹ��
	AVStream* audio_st;//��Ƶ����Ϣ�ṹ��
	struct SwsContext* sws_ctx;//����ת���������Ľṹ��
	AVIOContext* io_context;

	PacketQueue videoq;//��Ƶ�������ݰ�����(�������ݶ��У���������ʽʵ��)
	//������ͼ��֡����(�������ݶ��У������鷽ʽʵ��)����Ⱦ�߼��ͻ��pictq��ȡ���ݣ�ͬʱ�����߼��ֻ���pictqд������
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex, pictq_windex;//���г��ȣ���/дλ������
	SDL_mutex* pictq_lock;//���ж�д�����󣬱���ͼ��֡��������
	SDL_cond* pictq_ready;//���о�����������

	PacketQueue audioq;//��Ƶ�������ݰ�����(�������ݶ��У���������ʽʵ��)
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];//�������һ��packet��Ķ�֡ԭʼ��Ƶ����(�������ݶ��У������鷽ʽʵ��)
	unsigned int audio_buf_size;//�����Ķ�֡��Ƶ���ݳ���
	unsigned int audio_buf_index;//�ۼ�д��stream�ĳ���
	uint8_t* audio_pkt_data;//�������ݻ���ָ��λ��
	int audio_pkt_size;//������ʣ��ı������ݳ���(�Ƿ������һ��������pakcet���Ľ��룬һ�����ݰ��п��ܰ��������Ƶ����֡)

	int videoStream, audioStream;//����Ƶ�����ͱ��
	SDL_Thread* parse_tid;//�������ݰ������߳�id
	SDL_Thread* decode_tid;//�����߳�id

	char filename[1024];//�����ļ�����·����
	int quit;//ȫ���˳����̱�ʶ���ڽ����ϵ����˳��󣬸����߳��˳�
} VideoState;// Since we only have one decoding thread, the Big Struct can be global in case we need it.
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
	AVPacketList* pktlist = (AVPacketList*)av_malloc(sizeof(AVPacketList));//�ڶ��ϴ��������ڵ����
	if (!pktlist) {//��������ڵ�����Ƿ񴴽��ɹ�
		return -1;
	}
	pktlist->pkt = *pkt;//���������ݰ���ֵ���½������ڵ�����е����ݰ�����
	pktlist->next = NULL;//�������ָ��Ϊ��
//	if (av_packet_ref(pkt, pkt) < 0) {//����pkt�������ݵ����ü���(��������е�pkt���½������ڵ��е�pkt����ͬһ����ռ�)
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
	AVPacketList* pktlist;//��ʱ�����ڵ����ָ��
	int ret;//�������

	SDL_LockMutex(q->qlock);//���л�����������������������
	for (;;) {
		if (global_video_state->quit) {//����˳����̱�ʶ
			ret = -1;//����ʧ��
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
	}//end for for-loop
	SDL_UnlockMutex(q->qlock);//�ͷŻ�����
	return ret;
}
int queue_picture(VideoState* is, AVFrame* pFrame);
//��Ƶ�����̺߳���
int decode_thread(void* arg) {
	VideoState* is = (VideoState*)arg;//�����û�����
	AVPacket pkt, * packet = &pkt;//��ջ�ϴ�����ʱ���ݰ����󲢹���ָ��
	int frameFinished;//��������Ƿ�ɹ���ʶ

	// Allocate video frame��Ϊ��������Ƶ��Ϣ�ṹ�����ռ䲢��ɳ�ʼ������(�ṹ���е�ͼ�񻺴水�����������ֶ���װ)
	AVFrame* pFrame = av_frame_alloc();

	for (;;) {
		if (packet_queue_get(&is->videoq, packet, 1) < 0) {//�Ӷ�������ȡ���ݰ���packet��������ȡ�����ݰ�������
			// Means we quit getting packets.
			break;
		}
		/*-----------------------
		 * Decode video frame������������һ֡���ݣ�����frameFinished����Ϊtrue
		 * �����޷�ͨ��ֻ����һ��packet�ͻ��һ����������Ƶ֡frame��������Ҫ��ȡ���packet����
		 * avcodec_decode_video2()���ڽ��뵽������һ֡ʱ����frameFinishedΪ��
		 * Technically a packet can contain partial frames or other bits of data
		 * ffmpeg's parser ensures that the packets we get contain either complete or multiple frames
		 * convert the packet to a frame for us and set frameFinisned for us when we have the next frame
		 -----------------------*/
		avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished, packet);

		// Did we get a video frame������Ƿ���������һ֡ͼ��
		if (frameFinished) {
			if (queue_picture(is, pFrame) < 0) {//��������ɵ�ͼ��֡���ӵ�ͼ��֡����
				break;
			}
		}
		av_packet_unref(packet);//�ͷ�pkt�б���ı�������
	}
	av_free(pFrame);//���pFrame�е��ڴ�ռ�
	return 0;
}

//��Ƶ���뺯�����ӻ����������ȡ���ݰ������룬�����ؽ��������ݳ���(��һ��������packet���룬����������д��audio_buf���棬�����ض�֡�������ݵ��ܳ���)
int audio_decode_frame(VideoState* is) {
	int coded_consumed_size, data_size = 0;//ÿ�����ĵı������ݳ���[input](len1)�����ԭʼ��Ƶ���ݵĻ��泤��[output]
	AVPacket* pkt = &is->audio_pkt;//����Ӷ�������ȡ�����ݰ�

	for (;;) {
		while (is->audio_pkt_size > 0) {//��黺����ʣ��ı������ݳ���(�Ƿ������һ��������pakcet���Ľ��룬һ�����ݰ��п��ܰ��������Ƶ����֡)
			int got_frame = 0;//��������ɹ���ʶ���ɹ����ط���ֵ
			//����һ֡��Ƶ���ݣ����������ĵı������ݳ���
			coded_consumed_size = avcodec_decode_audio4(is->audio_st->codec, &is->audio_frame, &got_frame, pkt);
			if (coded_consumed_size < 0) {//����Ƿ�ִ���˽������
				// If error, skip frame.
				is->audio_pkt_size = 0;//���±������ݻ��泤��
				break;
			}
			if (got_frame) {//����������Ƿ�ɹ�
				//����������Ƶ���ݳ���[output]
				data_size = av_samples_get_buffer_size(NULL, is->audio_st->codec->channels, is->audio_frame.nb_samples, is->audio_st->codec->sample_fmt, 1);
				memcpy(is->audio_buf, is->audio_frame.data[0], data_size);//���������ݸ��Ƶ��������
			}
			is->audio_pkt_data += coded_consumed_size;//���±������ݻ���ָ��λ��
			is->audio_pkt_size -= coded_consumed_size;//���»�����ʣ��ı������ݳ���
			if (data_size <= 0) {//�������������ݻ��泤��
				// No data yet, get more frames.
				continue;
			}
			// We have data, return it and come back for more later.
			return data_size;//���ؽ������ݻ��泤��
		}
		if (pkt->data) {//������ݰ��Ƿ��ѴӶ�������ȡ
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
	}
}

/*------Audio Callback-------
 * ��Ƶ����ص�������sdlͨ���ûص�������������pcm����������������,
 * sdlͨ��һ�λ�׼��һ�黺��pcm���ݣ�ͨ���ûص���������������������Ƶpts���β���pcm����
 * �����뻺���pcm������ɲ��ź�������һ���µ�pcm��������(ÿ����Ƶ�������Ϊ��ʱ��sdl�͵��ô˺��������Ƶ������棬��������������)
 * When we begin playing audio, SDL will continually call this callback function
 * and ask it to fill the audio buffer with a certain number of bytes
 * The audio function callback takes the following parameters:
 * stream: A pointer to the audio buffer to be filled�������Ƶ���ݵ���������
 * len: The length (in bytes) of the audio buffer,���泤��wanted_spec.samples=SDL_AUDIO_BUFFER_SIZE(1024)
 --------------------------*/
void audio_callback(void* userdata, Uint8* stream, int len) {
	VideoState* is = (VideoState*)userdata;//�����û�����
	int wt_stream_len, audio_size;//ÿ��д��stream�����ݳ��ȣ����������ݳ���

	while (len > 0) {//�����Ƶ�����ʣ�೤��
		if (is->audio_buf_index >= is->audio_buf_size) {//����Ƿ���Ҫִ�н������
			// We have already sent all our data; get more���ӻ����������ȡ���ݰ������룬�����ؽ��������ݳ��ȣ�audio_buf�����п��ܰ�����֡��������Ƶ����
			audio_size = audio_decode_frame(is);
			if (audio_size < 0) {//����������Ƿ�ɹ�
				// If error, output silence.
				is->audio_buf_size = 1024;
				memset(is->audio_buf, 0, is->audio_buf_size);//ȫ�����û�����
			}
			else {
				is->audio_buf_size = audio_size;//����packet�а�����ԭʼ��Ƶ���ݳ���(��֡)
			}
			is->audio_buf_index = 0;//��ʼ���ۼ�д�뻺�泤��
		}//end for if

		wt_stream_len = is->audio_buf_size - is->audio_buf_index;//������뻺��ʣ�೤��
		if (wt_stream_len > len) {//���ÿ��д�뻺������ݳ����Ƿ񳬹�ָ������(1024)
			wt_stream_len = len;//ָ�����ȴӽ���Ļ�����ȡ����
		}

		//ÿ�δӽ���Ļ�����������ָ�����ȳ�ȡ���ݲ�д��stream���ݸ�����
		memcpy(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, wt_stream_len);
		len -= wt_stream_len;//���½�����Ƶ�����ʣ�೤��
		stream += wt_stream_len;//���»���д��λ��
		is->audio_buf_index += wt_stream_len;//�����ۼ�д�뻺�����ݳ���
	}//end for while
}

//����ָ�����ʹ������ҵ���Ӧ�Ľ�������������Ӧ����Ƶ���á�����ؼ���Ϣ�� VideoState��������Ƶ����Ƶ�߳�
int stream_component_open(VideoState* is, int stream_index) {
	AVFormatContext* pFormatCtx = is->pFormatCtx;//�����ļ������ķ�װ��Ϣ����������
	AVCodecContext* codecCtx = NULL;//�����������Ķ��󣬽�������������ػ�����״̬����Դ�Լ��������Ľӿ�ָ��
	AVCodec* codec = NULL;//������������Ϣ�Ľṹ�壬�ṩ���������Ĺ����ӿڣ����Կ����Ǳ��������������һ��ȫ�ֱ���
	SDL_AudioSpec wanted_spec, spec;//SDL_AudioSpec a structure that contains the audio output format������ SDL_AudioSpec �ṹ�壬������Ƶ��������
	AVDictionary* optionsDict = NULL;

	//���������������Ƿ��ں�����Χ��
	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
		return -1;
	}

	// Get a pointer to the codec context for the video stream.
	codecCtx = pFormatCtx->streams[stream_index]->codec;//ȡ�ý�����������

	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {//�������������Ƿ�Ϊ��Ƶ������
		// Set audio settings from codec info,SDL_AudioSpec a structure that contains the audio output format
		// ����SDL_AudioSpec�ṹ�壬������Ƶ���Ų���
		wanted_spec.freq = codecCtx->sample_rate;//����Ƶ�� DSP frequency -- samples per second
		wanted_spec.format = AUDIO_S16SYS;//������ʽ Audio data format
		wanted_spec.channels = codecCtx->channels;//������ Number of channels: 1 mono, 2 stereo
		wanted_spec.silence = 0;//�����ʱ�Ƿ���
		//Ĭ��ÿ�ζ���Ƶ����Ĵ�С���Ƽ�ֵΪ 512~8192��ffplayʹ�õ���1024 specifies a unit of audio data refers to the size of the audio buffer in sample frames
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;//���ö�ȡ��Ƶ���ݵĻص��ӿں��� the function to call when the audio device needs more data
		wanted_spec.userdata = is;//�����û�����

		/*---------------------------
		 * ��ָ����������Ƶ�豸����������ָ��������Ϊ�ӽ��Ĳ������ò���Ϊ�豸ʵ��֧�ֵ���Ƶ����
		 * Opens the audio device with the desired parameters(wanted_spec)
		 * return another specs we actually be using
		 * and not guaranteed to get what we asked for
		 --------------------------*/
		if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}
	}
	/*-----------------------
	 * Find the decoder for the video stream��������Ƶ����Ӧ�Ľ����������Ĳ��Ҷ�Ӧ�Ľ����������ض�Ӧ�Ľ�����(��Ϣ�ṹ��)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
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
		is->audio_buf_index = 0;//�ۼ�д��stream�ĳ���
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);//��Ƶ���ݰ����г�ʼ��
		SDL_PauseAudio(0);//audio callback starts running again��������Ƶ�豸�������ʱ��û�л��������ô���;���
		break;
	case AVMEDIA_TYPE_VIDEO://��Ƶ������
		is->videoStream = stream_index;//��Ƶ�����ͱ�ų�ʼ��
		is->video_st = pFormatCtx->streams[stream_index];
		packet_queue_init(&is->videoq);//��Ƶ���ݰ����г�ʼ��
		is->decode_tid = SDL_CreateThread(decode_thread, is);//���������߳�
		// Initialize SWS context for software scaling������ͼ��ת�����ظ�ʽΪAV_PIX_FMT_YUV420P
		is->sws_ctx = sws_getContext(is->video_st->codec->width, is->video_st->codec->height, is->video_st->codec->pix_fmt, is->video_st->codec->width, is->video_st->codec->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
		break;
	default:
		break;
	}
	return 0;
}

//��Ƶ(ͼ��)֡��Ⱦ
void video_display(VideoState* is) {
	SDL_Rect rect;//SDL���ζ���
	VideoPicture* vp;//ͼ��֡�ṹ��ָ��
	float aspect_ratio;//����/�߶ȱ�
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
		SDL_LockMutex(screen_lock);//������������������������������
		SDL_DisplayYUVOverlay(vp->bmp, &rect);//ͼ����Ⱦ
		SDL_UnlockMutex(screen_lock);//�ͷŻ�����
	}//end for if
}//end for video_display

//�޸�FFmpeg�ڲ��˳��ص���Ӧ�ĺ���
int decode_interrupt_cb(void* opaque) {
	return (global_video_state && global_video_state->quit);
}

//�������ݰ������̺߳���(����Ƶ�ļ��н���������Ƶ�������ݵ�Ԫ��һ��AVPacket��dataͨ����Ӧһ��NAL)
int parse_thread(void* arg) {
	VideoState* is = (VideoState*)arg;//�����û�����
	global_video_state = is;//����ȫ��״̬�����ṹ��
	AVFormatContext* pFormatCtx = NULL;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVPacket pkt, * packet = &pkt;//��ջ�ϴ�����ʱ���ݰ����󲢹���ָ��

	// Find the first video/audio stream.
	is->videoStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	is->audioStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	int video_index = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	int audio_index = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	int i;//ѭ������

	AVDictionary* io_dict = NULL;
	AVIOInterruptCB callback;
	// will interrupt blocking functions if we quit!.
	callback.callback = decode_interrupt_cb;
	callback.opaque = is;

	if (avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict)) {
		fprintf(stderr, "Unable to open I/O for %s\n", is->filename);
		return -1;
	}

	// Open video file������Ƶ�ļ���ȡ���ļ������ķ�װ��Ϣ����������
	if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0) {
		return -1; // Couldn't open file.
	}

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
		goto fail;//��ת���쳣����
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
		/*-----------------------
		 * read in a packet and store it in the AVPacket struct
		 * ffmpeg allocates the internal data for us,which is pointed to by packet.data
		 * this is freed by the av_free_packet()
		 -----------------------*/
		if (av_read_frame(is->pFormatCtx, packet) < 0) {
			if (is->pFormatCtx->pb->error == 0) {
				SDL_Delay(100); // No error; wait for user input.
				continue;
			}
			else {
				break;
			}
		}
		// Is this a packet from the video stream?
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

fail://�쳣����
	if (1) {
		SDL_Event event;//SDL�¼�����
		event.type = FF_QUIT_EVENT;//ָ���˳��¼�����
		event.user.data1 = is;//�����û�����
		SDL_PushEvent(&event);//�����¼�����ѹ��SDL��̨�¼�����
	}
	return 0;
}

//����/����ͼ��֡��Ϊͼ��֡�����ڴ�ռ�
void alloc_picture(void* userdata) {
	VideoState* is = (VideoState*)userdata;//�����û�����
	VideoPicture* vp = &is->pictq[is->pictq_windex];//��ͼ��֡����(����)����ȡͼ��֡�ṹ����
	if (vp->bmp) {//���ͼ��֡�Ƿ��Ѵ���
		// We already have one make another, bigger/smaller.
		SDL_FreeYUVOverlay(vp->bmp);//�ͷŵ�ǰoverlay����
	}
	// Allocate a place to put our YUV image on that screen.
	SDL_LockMutex(screen_lock);//������������������������������
	//����ָ���ߴ缰���ظ�ʽ���´������ػ�����
	vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width, is->video_st->codec->height, SDL_YV12_OVERLAY, screen);
	SDL_UnlockMutex(screen_lock);//�ͷŻ�����
	vp->width = is->video_st->codec->width;//����ͼ��֡����
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
 * 1�����ȼ��ͼ��֡����(����)�Ƿ���ڿռ�����µ�ͼ����û���㹻�Ŀռ����ͼ����ʹ��ǰ�߳����ߵȴ�
 * 2���ڳ�ʼ���������£�����(����)��VideoPicture��bmp����(YUV overlay)��δ����ռ䣬ͨ��FF_ALLOC_EVENT�¼��ķ�������alloc_picture����ռ�
 * 3��������(����)������VideoPicture��bmp����(YUV overlay)���ѷ���ռ������£�ֱ����������2��bmp���󿽱��������ݣ����������ڽ��и�ʽת����ִ�п�������
 ---------------------------*/
int queue_picture(VideoState* is, AVFrame* pFrame) {
	/*--------1���������Ƿ��в���ռ�-------*/
		// Wait until we have space for a new pic.
	SDL_LockMutex(is->pictq_lock);//����������������ͼ��֡����
	while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {//�����е�ǰ����
		SDL_CondWait(is->pictq_ready, is->pictq_lock);//�߳����ߵȴ�pictq_ready�ź�
	}
	SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����

	if (is->quit) {//�������˳���ʶ
		return -1;
	}
	/*-------2����ʼ��/����YUV overlay-------*/
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

		// Wait until we have a picture allocated.
		SDL_LockMutex(is->pictq_lock);//����������������ͼ��֡����
		while (!vp->allocated && !is->quit) {//��鵱ǰͼ��֡�Ƿ��ѳ�ʼ��(ΪSDL overlay)
			SDL_CondWait(is->pictq_ready, is->pictq_lock);//�߳����ߵȴ�alloc_picture����pictq_ready�źŻ��ѵ�ǰ�߳�
		}
		SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����
		if (is->quit) {//�������˳���ʶ
			return -1;
		}
	}//end for if
/*--------3��������Ƶ֡��YUV overlay-------*/
	AVFrame pict;//��ʱ����ת�����ͼ��֡���أ�������е�Ԫ�������
	// We have a place to put our picture on the queue.
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
		// Now we inform our display thread that we have a pic ready.
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {//���²���鵱ǰͼ��֡����д��λ��
			is->pictq_windex = 0;//����ͼ��֡����д��λ��
		}
		SDL_LockMutex(is->pictq_lock);//�������ж�д����������������
		is->pictq_size++;//����ͼ��֡���г���
		SDL_UnlockMutex(is->pictq_lock);//�ͷŶ��ж�д��
	}//end for if
	return 0;
}

//��ʱ�������Ļص�����
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque) {
	SDL_Event event;//SDL�¼�����
	event.type = FF_REFRESH_EVENT;//��Ƶ��ʾˢ���¼�
	event.user.data1 = opaque;//�����û�����
	SDL_PushEvent(&event);//�����¼�
	return 0; // 0 means stop timer.
}

/*---------------------------
 * Schedule a video refresh in 'delay' ms.
 * ����sdl��ָ������ʱ��������һ�� FF_REFRESH_EVENT �¼�
 * ����¼������¼������ﴥ��sdl_refresh_timer_cb�����ĵ���
 --------------------------*/
static void schedule_refresh(VideoState* is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);//��ָ����ʱ��(ms)��ص��û�ָ���ĺ���
}

//��ʾˢ�º���(FF_REFRESH_EVENT��Ӧ����)
void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;//�����û�����
	// vp is used in later tutorials for synchronization.
	if (is->video_st) {
		if (is->pictq_size == 0) {//���ͼ��֡�����Ƿ��д���ʾͼ��
			schedule_refresh(is, 1);
		}
		else {//ˢ��ͼ��
		 /*-------------------------
		  * Now, normally here goes a ton of code about timing, etc.
		  * we're just going to guess at a delay for now.
		  * You can increase and decrease this value and hard code the timing
		  * but I don't suggest that ;) We'll learn how to do it for real later..
		  ------------------------*/
			schedule_refresh(is, 40);//������ʾ��һ֡ͼ���ˢ��ʱ�䣬ͨ����ʱ��timer��ʽ����
			// Show the picture!
			video_display(is);//ͼ��֡��Ⱦ
			// Update queue for next picture!
			if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {//���²����ͼ��֡���ж�λ������
				is->pictq_rindex = 0;//���ö�λ������
			}
			SDL_LockMutex(is->pictq_lock);//������������������������������
			is->pictq_size--;//����ͼ��֡���г���
			SDL_CondSignal(is->pictq_ready);//���Ͷ��о����ź�
			SDL_UnlockMutex(is->pictq_lock);//�ͷŻ�����
		}
	}
	else {
		schedule_refresh(is, 100);
	}
}

int main(int argc, char* argv[]) {
	//if (argc < 2) {//���������������Ƿ���ȷ
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}
	// Register all formats and codecs��ע������֧�ֵĶ�ý���ʽ���������
	av_register_all();

	VideoState* is = (VideoState*)av_mallocz(sizeof(VideoState));//����ȫ��״̬����
	av_strlcpy(is->filename, fileName, sizeof(is->filename));//������Ƶ�ļ�·����
	screen_lock = SDL_CreateMutex();//�������ػ�����������
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

	is->parse_tid = SDL_CreateThread(parse_thread, is);//�����������ݰ������߳�
	if (!is->parse_tid) {//����߳��Ƿ񴴽��ɹ�
		av_free(is);
		return -1;
	}

	SDL_Event event;//SDL�¼�����
	for (;;) {//SDL�¼�ѭ��
		SDL_WaitEvent(&event);//Use this function to wait indefinitely for the next available event�����߳��������ȴ��¼�����
		switch (event.type) {//�¼������������̣߳�����¼�����
		case FF_QUIT_EVENT:
		case SDL_QUIT://�˳������¼�
			is->quit = 1;
			// If the video has finished playing, then both the picture and audio queues are waiting for more data.  
			// Make them stop waiting and terminate normally..
			SDL_CondSignal(is->audioq.qready);//�������о����źű�������
			SDL_CondSignal(is->videoq.qready);
			SDL_Quit();
			return 0;
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
	system("pause");
	return 0;
}
