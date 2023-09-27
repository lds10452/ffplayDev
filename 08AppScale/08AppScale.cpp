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
//1、消息队列处理函数在处理消息前，先对互斥量进行锁定，以保护消息队列中的临界区资源
//2、若消息队列为空，则调用pthread_cond_wait对互斥量暂时解锁，等待其他线程向消息队列中插入消息数据
//3、待其他线程向消息队列中插入消息数据后，通过pthread_cond_signal像等待线程发出qready信号
//4、消息队列处理线程收到qready信号被唤醒，重新获得对消息队列临界区资源的独占

#include <pthread.h>

struct msg{//消息队列结构体
	struct msg *m_next;//消息队列后继节点
	//more stuff here
}

struct msg *workq;//消息队列指针
pthread_cond_t qready=PTHREAD_COND_INITIALIZER;//消息队列就绪条件变量
pthread_mutex_t qlock=PTHREAS_MUTEX_INITIALIZER;//消息队列互斥量，保护消息队列数据

//消息队列处理函数
void process_msg(void){
	struct msg *mp;//消息结构指针
	for(;;){
		pthread_mutex_lock(&qlock);//消息队列互斥量加锁，保护消息队列数据
		while(workq==NULL){//检查消息队列是否为空，若为空
			pthread_cond_wait(&qready,&qlock);//等待消息队列就绪信号qready，并对互斥量暂时解锁，该函数返回时，互斥量再次被锁住
		}
		mp=workq;//线程醒来，从消息队列中取数据准备处理
		workq=mp->m_next;//更新消息队列，指针后移清除取出的消息
		pthread_mutex_unlock(&qlock);//释放锁
		//now process the message mp
	}
}

//将消息插入消息队列
void enqueue_msg(struct msg *mp){
	pthread_mutex_lock(&qlock);//消息队列互斥量加锁，保护消息队列数据
	mp->m_next=workq;//将原队列头作为插入消息的后继节点
	workq=mp;//将新消息插入队列
	pthread_cond_signal(&qready);//给等待线程发出qready消息，通知消息队列已就绪
	pthread_mutex_unlock(&qlock);//释放锁
}
---------------------------*/

#include "SDL.h"
extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
	#include "libavutil/imgutils.h"
	#include "libswresample/swresample.h"
}
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
#include<iostream>
#include<assert.h>
#include <stdio.h>
using namespace std;
const char* filenName = "../data/test.mp4";

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

using namespace std;

typedef struct PacketQueue
{
	AVPacketList* first_pkt; // 队头
	AVPacketList* last_pkt; // 队尾

	int nb_packets; //包的个数
	int size; // 占用空间的字节数
	SDL_mutex* mutext; // 互斥信号量
	SDL_cond* cond; // 条件变量
}PacketQueue;

PacketQueue audioq;
int quit = 0;

AVFrame wanted_frame;

// 包队列初始化
void packet_queue_init(PacketQueue* q)
{
	//memset(q, 0, sizeof(PacketQueue));
	q->last_pkt = nullptr;
	q->first_pkt = nullptr;
	q->mutext = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

// 放入packet到队列中，不带头指针的队列
int packet_queue_put(PacketQueue* q, AVPacket* pkt)
{
	AVPacketList* pktl;
	if (av_dup_packet(pkt) < 0)
		return -1;

	pktl = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pktl)
		return -1;

	pktl->pkt = *pkt;
	pktl->next = nullptr;

	SDL_LockMutex(q->mutext);

	if (!q->last_pkt) // 队列为空，新插入元素为第一个元素
		q->first_pkt = pktl;
	else // 插入队尾
		q->last_pkt->next = pktl;

	q->last_pkt = pktl;

	q->nb_packets++;
	q->size += pkt->size;

	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutext);

	return 0;
}

// 从队列中取出packet
static int packet_queue_get(PacketQueue* q, AVPacket* pkt, bool block)
{
	AVPacketList* pktl;
	int ret;

	SDL_LockMutex(q->mutext);

	while (true)
	{
		if (quit)
		{
			ret = -1;
			break;
		}

		pktl = q->first_pkt;
		if (pktl)
		{
			q->first_pkt = pktl->next;
			if (!q->first_pkt)
				q->last_pkt = nullptr;

			q->nb_packets--;
			q->size -= pktl->pkt.size;

			*pkt = pktl->pkt;
			av_free(pktl);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(q->cond, q->mutext);
		}
	}

	SDL_UnlockMutex(q->mutext);

	return ret;
}

// 解码音频数据
int audio_decode_frame(AVCodecContext* aCodecCtx, uint8_t* audio_buf, int buf_size)
{
	static AVPacket pkt;
	static uint8_t* audio_pkt_data = nullptr;
	static int audio_pkt_size = 0;
	static AVFrame frame;

	int len1;
	int data_size = 0;

	SwrContext* swr_ctx = nullptr;

	while (true)
	{
		while (audio_pkt_size > 0)
		{
			int got_frame = 0;
			len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
			if (len1 < 0) // 出错，跳过
			{
				audio_pkt_size = 0;
				break;
			}

			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			data_size = 0;
			if (got_frame)
			{
				data_size = av_samples_get_buffer_size(nullptr, aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt, 1);
				assert(data_size <= buf_size);
				memcpy(audio_buf, frame.data[0], data_size);
			}

			if (frame.channels > 0 && frame.channel_layout == 0)
				frame.channel_layout = av_get_default_channel_layout(frame.channels);
			else if (frame.channels == 0 && frame.channel_layout > 0)
				frame.channels = av_get_channel_layout_nb_channels(frame.channel_layout);

			if (swr_ctx)
			{
				swr_free(&swr_ctx);
				swr_ctx = nullptr;
			}

			swr_ctx = swr_alloc_set_opts(nullptr, wanted_frame.channel_layout, (AVSampleFormat)wanted_frame.format, wanted_frame.sample_rate,
				frame.channel_layout, (AVSampleFormat)frame.format, frame.sample_rate, 0, nullptr);

			if (!swr_ctx || swr_init(swr_ctx) < 0)
			{
				cout << "swr_init failed:" << endl;
				break;
			}

			int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame.sample_rate) + frame.nb_samples,
				wanted_frame.sample_rate, wanted_frame.format, AVRounding(1));
			int len2 = swr_convert(swr_ctx, &audio_buf, dst_nb_samples,
				(const uint8_t**)frame.data, frame.nb_samples);
			if (len2 < 0)
			{
				cout << "swr_convert failed\n";
				break;
			}

			return wanted_frame.channels * len2 * av_get_bytes_per_sample((AVSampleFormat)wanted_frame.format);

			if (data_size <= 0)
				continue; // No data yet,get more frames

			return data_size; // we have data,return it and come back for more later
		}

		if (pkt.data)
			av_free_packet(&pkt);

		if (quit)
			return -1;

		if (packet_queue_get(&audioq, &pkt, true) < 0)
			return -1;

		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}
}



// 解码后的回调函数
void audio_callback(void* userdata, Uint8* stream, int len)
{
	AVCodecContext* aCodecCtx = (AVCodecContext*)userdata;
	int len1, audio_size;

	static uint8_t audio_buff[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	SDL_memset(stream, 0, len);

	while (len > 0)
	{
		if (audio_buf_index >= audio_buf_size)
		{
			audio_size = audio_decode_frame(aCodecCtx, audio_buff, sizeof(audio_buff));
			if (audio_size < 0)
			{
				audio_buf_size = 1024;
				memset(audio_buff, 0, audio_buf_size);
			}
			else
				audio_buf_size = audio_size;

			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;

		SDL_MixAudio(stream, audio_buff + audio_buf_index, len, SDL_MIX_MAXVOLUME);


		//memcpy(stream, (uint8_t*)(audio_buff + audio_buf_index), audio_buf_size);
		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}
}

int main(int argv, char* argc[])
{
	//1.注册支持的文件格式及对应的codec
	av_register_all();

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

	//	char* filenName = "F:\\test.rmvb";



		// 2.打开文件，读取流信息
	AVFormatContext* pFormatCtx = nullptr;
	// 读取文件头，将格式相关信息存放在AVFormatContext结构体中
	if (avformat_open_input(&pFormatCtx, filenName, nullptr, nullptr) != 0)
		return -1; // 打开失败

	// 检测文件的流信息
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0)
		return -1; // 没有检测到流信息 stream infomation

	// 在控制台输出文件信息
	av_dump_format(pFormatCtx, 0, filenName, 0);

	//查找第一个视频流 video stream
	int audioStream = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioStream = i;
			break;
		}
	}

	// 3. 根据读取到的流信息查找相应的解码器并打开
	if (audioStream == -1)
		return -1; // 没有查找到视频流audio stream

	AVCodecContext* pCodecCtxOrg = nullptr;
	AVCodecContext* pCodecCtx = nullptr;

	AVCodec* pCodec = nullptr;

	pCodecCtxOrg = pFormatCtx->streams[audioStream]->codec; // codec context

	// 找到audio stream的 decoder
	pCodec = avcodec_find_decoder(pCodecCtxOrg->codec_id);

	if (!pCodec)
	{
		cout << "Unsupported codec!" << endl;
		return -1;
	}

	// 不直接使用从AVFormatContext得到的CodecContext，要复制一个
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrg) != 0)
	{
		cout << "Could not copy codec context!" << endl;
		return -1;
	}


	// Set audio settings from codec info
	SDL_AudioSpec wanted_spec, spec;
	wanted_spec.freq = pCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = pCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = pCodecCtx;

	if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
	{
		cout << "Open audio failed:" << SDL_GetError() << endl;
		getchar();
		return -1;
	}

	wanted_frame.format = AV_SAMPLE_FMT_S16;
	wanted_frame.sample_rate = spec.freq;
	wanted_frame.channel_layout = av_get_default_channel_layout(spec.channels);
	wanted_frame.channels = spec.channels;

	avcodec_open2(pCodecCtx, pCodec, nullptr);

	packet_queue_init(&audioq);
	SDL_PauseAudio(0);

	AVPacket packet;
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		if (packet.stream_index == audioStream)
			packet_queue_put(&audioq, &packet);
		else
			av_free_packet(&packet);
	}

	getchar();
	return 0;
}
