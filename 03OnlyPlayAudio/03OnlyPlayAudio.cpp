// tutorial03.c
// A pedagogical video player that will stream through every video frame as fast as it can
// and play audio (out of sync).
//
// This tutorial was written by Stephen Dranger (dranger@gmail.com).
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
//
// Updates tested on:
// Mac OS X 10.11.6
// Apple LLVM version 8.0.0 (clang-800.0.38)
//
// Use 
//
// $ gcc -o tutorial03 tutorial03.c -lavutil -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
//
// to build (assuming libavutil/libavformat/libavcodec/libswscale are correctly installed your system).
//
// Run using
//
// $ tutorial03 myvideofile.mpg
//
// to play the stream on your screen with voice.

/*---------------------------
//1����Ϣ���д������ڴ�����Ϣǰ���ȶԻ����������������Ա�����Ϣ�����е��ٽ�����Դ
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

//��Ϣ���д�����
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
	#include "libavutil/imgutils.h"
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
const char* fileName = "../data/test.mp4";

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

int quit = 0;//ȫ���˳����̱�ʶ���ڽ����ϵ����˳��󣬸����߳��˳�
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
PacketQueue audioq;//����ȫ�ֶ��ж���

//���г�ʼ������
void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));//ȫ���ʼ�����нṹ�����
	q->qlock = SDL_CreateMutex();//��������������
	q->qready = SDL_CreateCond();//����������������
}

//������в������ݰ�
int packet_queue_put(PacketQueue* q, AVPacket* pkt) {
	/*-------׼������(����)�ڵ����------*/
	AVPacketList* pktlist;//��������ڵ����ָ��
	pktlist = (AVPacketList*)av_malloc(sizeof(AVPacketList));//�ڶ��ϴ�������ڵ����
	if (!pktlist) {//�������ڵ�����Ƿ񴴽��ɹ�
		return -1;
	}
	pktlist->pkt = *pkt;//���������ݰ���ֵ���½�����ڵ�����е����ݰ�����
	pktlist->next = NULL;//������ָ��Ϊ��
//	if (av_packet_ref(pkt, pkt)<0) {//����pkt�������ݵ����ü���(��������е�pkt���½�����ڵ��е�pkt����ͬһ����ռ�)
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
		if (quit) {//����˳����̱�ʶ
			ret = -1;//����ʧ��
			break;
		}

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

/*---------------------------
 * �ӻ����������ȡ���ݰ������룬�����ؽ��������ݳ���(��һ��������packet���룬����������д��audio_buf���棬�����ض�֡�������ݵ��ܳ���)
 * aCodecCtx:��Ƶ������������
 * audio_buf���������һ��������packe���ԭʼ��Ƶ����(�����п��ܰ�����֡��������Ƶ����)
 * buf_size����������Ƶ���ݳ��ȣ�δʹ��
 --------------------------*/
int audio_decode_frame(AVCodecContext* aCodecCtx, uint8_t* audio_buf, int buf_size) {
	static AVPacket pkt;//����Ӷ�������ȡ�����ݰ�
	static AVFrame frame;//��������ݰ��н������Ƶ����
	static uint8_t* audio_pkt_data = NULL;//�������ݰ��������ݻ���ָ��
	static int audio_pkt_size = 0;//���ݰ���ʣ��ı������ݳ���
	int coded_consumed_size, data_size = 0;//ÿ�����ĵı������ݳ���[input](len1)�����ԭʼ��Ƶ���ݵĻ��泤��[output]

	for (;;) {
		while (audio_pkt_size > 0) {//��黺����ʣ��ı������ݳ���(�Ƿ������һ��������pakcet���Ľ��룬һ�����ݰ��п��ܰ��������Ƶ����֡)
			int got_frame = 0;//��������ɹ���ʶ���ɹ����ط���ֵ
			coded_consumed_size = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);//����һ֡��Ƶ���ݣ����������ĵı������ݳ���
			if (coded_consumed_size < 0) {//����Ƿ�ִ���˽������
				// if error, skip frame.
				audio_pkt_size = 0;//���±������ݻ��泤��
				break;
			}
			audio_pkt_data += coded_consumed_size;//���±������ݻ���ָ��λ��
			audio_pkt_size -= coded_consumed_size;//���»�����ʣ��ı������ݳ���
			if (got_frame) {//����������Ƿ�ɹ�
				//����������Ƶ���ݳ���[output]
				data_size = av_samples_get_buffer_size(NULL, aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt, 1);
				memcpy(audio_buf, frame.data[0], data_size);//���������ݸ��Ƶ��������
			}
			if (data_size <= 0) {//�������������ݻ��泤��
				// No data yet, get more frames.
				continue;
			}
			// We have data, return it and come back for more later.
			return data_size;//���ؽ������ݻ��泤��
		}//end for while

		if (pkt.data) {//������ݰ��Ƿ��ѴӶ�������ȡ
			av_packet_unref(&pkt);//�ͷ�pkt�б���ı�������
		}

		if (quit) {//����˳����̱�ʶ
			return -1;
		}
		//�Ӷ�������ȡ���ݰ���pkt
		if (packet_queue_get(&audioq, &pkt, 1) < 0) {
			return -1;
		}
		audio_pkt_data = pkt.data;//���ݱ������ݻ���ָ��
		audio_pkt_size = pkt.size;//���ݱ������ݻ��泤��
	}//end for for-loop
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
	AVCodecContext* aCodecCtx = (AVCodecContext*)userdata;//�����û�����
	int wt_stream_len, audio_size;//ÿ��д��stream�����ݳ��ȣ����������ݳ���

	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];//�������һ��packet��Ķ�֡ԭʼ��Ƶ����
	static unsigned int audio_buf_size = 0;//�����Ķ�֡��Ƶ���ݳ���
	static unsigned int audio_buf_index = 0;//�ۼ�д��stream�ĳ���

	while (len > 0) {//�����Ƶ�����ʣ�೤��
		if (audio_buf_index >= audio_buf_size) {//����Ƿ���Ҫִ�н������
			// We have already sent all our data; get more���ӻ����������ȡ���ݰ������룬�����ؽ��������ݳ��ȣ�audio_buf�����п��ܰ�����֡��������Ƶ����
			audio_size = audio_decode_frame(aCodecCtx, audio_buf, audio_buf_size);
			if (audio_size < 0) {//����������Ƿ�ɹ�
				// If error, output silence.
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);//ȫ�����û�����
			}
			else {
				audio_buf_size = audio_size;//����packet�а�����ԭʼ��Ƶ���ݳ���(��֡)
			}
			audio_buf_index = 0;//��ʼ���ۼ�д�뻺�泤��
		}//end for if

		wt_stream_len = audio_buf_size - audio_buf_index;//������뻺��ʣ�೤��
		if (wt_stream_len > len) {//���ÿ��д�뻺������ݳ����Ƿ񳬹�ָ������(1024)
			wt_stream_len = len;//ָ�����ȴӽ���Ļ�����ȡ����
		}
		//ÿ�δӽ���Ļ�����������ָ�����ȳ�ȡ���ݲ�д��stream���ݸ�����
		memcpy(stream, (uint8_t*)audio_buf + audio_buf_index, wt_stream_len);
		len -= wt_stream_len;//���½�����Ƶ�����ʣ�೤��
		stream += wt_stream_len;//���»���д��λ��
		audio_buf_index += wt_stream_len;//�����ۼ�д�뻺�����ݳ���
	}//end for while
}

int main(int argc, char* argv[]) {
	/*--------------��������-------------*/
	AVFormatContext* pFormatCtx = NULL;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVCodecContext* vCodecCtx = NULL;//��Ƶ�����������Ķ��󣬽�������������ػ�����״̬����Դ�Լ��������Ľӿ�ָ��
	AVCodecContext* aCodecCtx = NULL;//��Ƶ�����������Ķ��󣬽�������������ػ�����״̬����Դ�Լ��������Ľӿ�ָ��
	AVCodec* vCodec = NULL;//������Ƶ���������Ϣ�Ľṹ�壬�ṩ���������Ĺ����ӿڣ����Կ����Ǳ��������������һ��ȫ�ֱ���
	AVCodec* aCodec = NULL;//������Ƶ���������Ϣ�Ľṹ�壬�ṩ���������Ĺ����ӿڣ����Կ����Ǳ��������������һ��ȫ�ֱ���
	AVPacket packet;//���𱣴�ѹ���������������Ϣ�Ľṹ��,ÿ֡ͼ����һ�����packet�����
	AVFrame* pFrame = NULL;//��������Ƶ���������ݣ���״̬��Ϣ�����������Ϣ��������ͱ�QP���˶�ʸ���������
	struct SwsContext* sws_ctx = NULL;//����ת���������Ľṹ��
	AVDictionary* videoOptionsDict = NULL;
	AVDictionary* audioOptionsDict = NULL;

	SDL_Surface* screen = NULL;//SDL��ͼ���棬A structure that contains a collection of pixels used in software blitting
	SDL_Overlay* bmp = NULL;//SDL����
	SDL_Rect rect;//SDL���ζ���
	SDL_AudioSpec wanted_spec, spec;//SDL_AudioSpec a structure that contains the audio output format������ SDL_AudioSpec �ṹ�壬������Ƶ��������
	SDL_Event event;//SDL�¼�����

	int i, videoStream, audioStream;//ѭ������������Ƶ�����ͱ��
	int frameFinished;//��������Ƿ�ɹ���ʶ

/*-------------������ʼ��------------*/
	//if (argc < 2) {//���������������Ƿ���ȷ
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}
	// Register all formats and codecs��ע�����ж�ý���ʽ���������
	av_register_all();

	// Open video file������Ƶ�ļ���ȡ���ļ������ķ�װ��Ϣ����������
	if (avformat_open_input(&pFormatCtx, fileName, NULL, NULL) != 0) {
		return -1; // Couldn't open file.
	}

	// Retrieve stream information��ȡ���ļ��б����������Ϣ
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		return -1; // Couldn't find stream information.
	}

	// Dump information about file onto standard error����ӡpFormatCtx�е�������Ϣ
	av_dump_format(pFormatCtx, 0, fileName, 0);

	// Find the first video stream.
	videoStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	audioStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	for (i = 0; i < pFormatCtx->nb_streams; i++) {//�����ļ��а�����������ý������(��Ƶ������Ƶ������Ļ����)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0) {//���ļ��а�������Ƶ��
			videoStream = i;//����Ƶ�����͵ı���޸ı�ʶ��ʹ֮��Ϊ-1
		}
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0) {//���ļ��а�������Ƶ��
			audioStream = i;//����Ƶ�����͵ı���޸ı�ʶ��ʹ֮��Ϊ-1
		}
	}
	if (videoStream == -1) {//����ļ����Ƿ������Ƶ��
		return -1; // Didn't find a video stream.
	}
	if (audioStream == -1) {//����ļ����Ƿ������Ƶ��
		return -1;
	}

	// Get a pointer to the codec context for the video stream�����������ͱ�Ŵ�pFormatCtx->streams��ȡ����Ƶ����Ӧ�Ľ�����������
	vCodecCtx = pFormatCtx->streams[videoStream]->codec;
	/*-----------------------
	 * Find the decoder for the video stream��������Ƶ����Ӧ�Ľ����������Ĳ��Ҷ�Ӧ�Ľ����������ض�Ӧ�Ľ�����(��Ϣ�ṹ��)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
	vCodec = avcodec_find_decoder(vCodecCtx->codec_id);
	if (vCodec == NULL) {//���������Ƿ�ƥ��
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found.
	}
	if (avcodec_open2(vCodecCtx, vCodec, &videoOptionsDict) < 0)// Open codec������Ƶ������
		return -1; // Could not open codec.

	// Get a pointer to the codec context for the video stream�����������ͱ�Ŵ�pFormatCtx->streams��ȡ����Ƶ����Ӧ�Ľ�����������
	aCodecCtx = pFormatCtx->streams[audioStream]->codec;
	// Find the decoder for the video stream��������Ƶ����Ӧ�Ľ����������Ĳ��Ҷ�Ӧ�Ľ����������ض�Ӧ�Ľ�����(��Ϣ�ṹ��)
	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if (!aCodec) {//���������Ƿ�ƥ��
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}
	avcodec_open2(aCodecCtx, aCodec, &audioOptionsDict);// Open codec������Ƶ������

	// Allocate video frame��Ϊ��������Ƶ��Ϣ�ṹ�����ռ䲢��ɳ�ʼ������(�ṹ���е�ͼ�񻺴水�����������ֶ���װ)
	pFrame = av_frame_alloc();
	// Initialize SWS context for software scaling������ͼ��ת�����ظ�ʽΪAV_PIX_FMT_YUV420P
	sws_ctx = sws_getContext(vCodecCtx->width, vCodecCtx->height, vCodecCtx->pix_fmt, vCodecCtx->width, vCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

	packet_queue_init(&audioq);//������г�ʼ��

	//SDL_Init initialize the Event Handling, File I/O, and Threading subsystems����ʼ��SDL 
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {//initialize the video audio & timer subsystem 
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		exit(1);
	}
	// Make a screen to put our video,��SDL2.0��SDL_SetVideoMode��SDL_Overlay�Ѿ����ã���ΪSDL_CreateWindow��SDL_CreateRenderer�������ڼ���ɫ��
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(vCodecCtx->width, vCodecCtx->height, 0, 0);//����SDL���ڼ���ͼ���棬��ָ��ͼ��ߴ缰���ظ���
#else
	screen = SDL_SetVideoMode(vCodecCtx->width, vCodecCtx->height, 24, 0);//����SDL���ڼ���ͼ���棬��ָ��ͼ��ߴ缰���ظ���
#endif
	if (!screen) {//���SDL(��ͼ����)�����Ƿ񴴽��ɹ�(SDL�û�ͼ��������������)
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}
	SDL_WM_SetCaption(argv[1], 0);//�������ļ�������SDL���ڱ���

	// Allocate a place to put our YUV image on that screen��������������
	bmp = SDL_CreateYUVOverlay(vCodecCtx->width, vCodecCtx->height, SDL_YV12_OVERLAY, screen);

	// Set audio settings from codec info,SDL_AudioSpec a structure that contains the audio output format
	// ����SDL_AudioSpec�ṹ�壬������Ƶ���Ų���
	wanted_spec.freq = aCodecCtx->sample_rate;//����Ƶ�� DSP frequency -- samples per second
	wanted_spec.format = AUDIO_S16SYS;//������ʽ Audio data format
	wanted_spec.channels = aCodecCtx->channels;//������ Number of channels: 1 mono, 2 stereo
	wanted_spec.silence = 0;//�����ʱ�Ƿ���
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;//Ĭ��ÿ�ζ���Ƶ����Ĵ�С���Ƽ�ֵΪ 512~8192��ffplayʹ�õ���1024 specifies a unit of audio data refers to the size of the audio buffer in sample frames
	wanted_spec.callback = audio_callback;//����ȡ��Ƶ���ݵĻص��ӿں��� the function to call when the audio device needs more data
	wanted_spec.userdata = aCodecCtx;//�����û�����

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
	SDL_PauseAudio(0);//audio callback starts running again��������Ƶ�豸�������ʱ��û�л��������ô���;���
/*--------------ѭ������-------------*/
	i = 0;// Read frames and save first five frames to disk.
	/*-----------------------
	 * read in a packet and store it in the AVPacket struct
	 * ffmpeg allocates the internal data for us,which is pointed to by packet.data
	 * this is freed by the av_free_packet()
	 -----------------------*/
	while (av_read_frame(pFormatCtx, &packet) >= 0) {//���ļ������ζ�ȡÿ��ͼ��������ݰ������洢��AVPacket���ݽṹ��
		// Is this a packet from the video stream��������ݰ�����
		if (packet.stream_index == videoStream) {//�����Ƶý�������ͱ�ʶ
		   /*-----------------------
			* Decode video frame������������һ֡���ݣ�����frameFinished����Ϊtrue
			* �����޷�ͨ��ֻ����һ��packet�ͻ��һ����������Ƶ֡frame��������Ҫ��ȡ���packet����
			* avcodec_decode_video2()���ڽ��뵽������һ֡ʱ����frameFinishedΪ��
			* Technically a packet can contain partial frames or other bits of data
			* ffmpeg's parser ensures that the packets we get contain either complete or multiple frames
			* convert the packet to a frame for us and set frameFinisned for us when we have the next frame
			-----------------------*/
			avcodec_decode_video2(vCodecCtx, pFrame, &frameFinished, &packet);

			// Did we get a video frame������Ƿ���������һ֡ͼ��
			if (frameFinished) {
				SDL_LockYUVOverlay(bmp);//locks the overlay for direct access to pixel data��ԭ�Ӳ������������ػ�����������Ƿ��޸�

				AVFrame pict;//����ת��ΪAV_PIX_FMT_YUV420P��ʽ����Ƶ֡
				pict.data[0] = bmp->pixels[0];//��ת����ͼ���뻭�������ػ���������
				pict.data[1] = bmp->pixels[2];
				pict.data[2] = bmp->pixels[1];

				pict.linesize[0] = bmp->pitches[0];//��ת����ͼ��ɨ���г����뻭�����ػ�������ɨ���г��������
				pict.linesize[1] = bmp->pitches[2];//linesize-Size, in bytes, of the data for each picture/channel plane
				pict.linesize[2] = bmp->pitches[1];;//For audio, only linesize[0] may be set

				// Convert the image into YUV format that SDL uses����������ͼ��ת��ΪAV_PIX_FMT_YUV420P��ʽ������ֵ��pict����
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, vCodecCtx->height, pict.data, pict.linesize);

				SDL_UnlockYUVOverlay(bmp);//Unlocks a previously locked overlay. An overlay must be unlocked before it can be displayed
				//���þ�����ʾ����
				rect.x = 0;
				rect.y = 0;
				rect.w = vCodecCtx->width;
				rect.h = vCodecCtx->height;
				SDL_DisplayYUVOverlay(bmp, &rect);//ͼ����Ⱦ
				av_packet_unref(&packet);//Free the packet that was allocated by av_read_frame���ͷ�AVPacket���ݽṹ�б�������ָ��
			}
		}
		else if (packet.stream_index == audioStream) {//�����Ƶý�������ͱ�ʶ
			packet_queue_put(&audioq, &packet);//�򻺴���������������ݰ�
		}
		else {//��Ļ�����ͱ�ʶ
		 //Free the packet that was allocated by av_read_frame���ͷ�AVPacket���ݽṹ�б�������ָ��
			av_packet_unref(&packet);
		}

		/*-------------------------
		 * ��ÿ��ѭ���д�SDL��̨����ȡ�¼�����䵽SDL_Event������
		 * SDL���¼�ϵͳʹ������Խ����û������룬�Ӷ����һЩ���Ʋ���
		 * SDL_PollEvent() is the favored way of receiving system events
		 * since it can be done from the main loop and does not suspend the main loop
		 * while waiting on an event to be posted
		 * poll for events right after we finish processing a packet
		 ------------------------*/
		SDL_PollEvent(&event);
		switch (event.type) {//���SDL�¼�����
		case SDL_QUIT://�˳��¼�
			quit = 1;//�˳����̱�ʶ��1
			SDL_Quit();//�˳�����
			exit(0);//��������
			break;
		default:
			break;
		}//end for switch
	}//end for while
/*--------------��������-------------*/
	// Free the YUV frame.
	av_free(pFrame);

	// Close the codec.
	avcodec_close(vCodecCtx);

	// Close the video file.
	avformat_close_input(&pFormatCtx);
	system("pause");
	return 0;
}
