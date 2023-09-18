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

SDL_Surface* screen;//SDL绘图表面，A structure that contains a collection of pixels used in software blitting
SDL_mutex* screen_lock;//SDL互斥锁对象，在图像帧渲染的时候，保护绘图表面的像素数据不被意外修改

/*-------链表节点结构体--------
typedef struct AVPacketList {
	AVPacket pkt;//链表数据
	struct AVPacketList *next;//链表后继节点
} AVPacketList;
---------------------------*/
//数据包队列(链表)结构体
typedef struct PacketQueue {
	AVPacketList* first_pkt, * last_pkt;//队列首尾节点指针
	int nb_packets;//队列长度
	int size;//保存编码数据的缓存长度，size=packet->size
	SDL_mutex* qlock;//队列互斥量，保护队列数据
	SDL_cond* qready;//队列就绪条件变量
} PacketQueue;

//图像帧结构体
typedef struct VideoPicture {
	SDL_Overlay* bmp;//SDL画布overlay
	int width, height;//Source height & width.
	int allocated;//是否分配内存空间，视频帧转换为SDL overlay标识
} VideoPicture;

typedef struct VideoState {
	AVFormatContext* pFormatCtx;//保存文件容器封装信息及码流参数的结构体
	AVPacket audio_pkt;//保存从队列中提取的数据包
	AVFrame audio_frame;//保存从数据包中解码的音频数据
	AVStream* video_st;//视频流信息结构体
	AVStream* audio_st;//音频流信息结构体
	struct SwsContext* sws_ctx;//描述转换器参数的结构体
	AVIOContext* io_context;

	PacketQueue videoq;//视频编码数据包队列(编码数据队列，以链表方式实现)
	//解码后的图像帧队列(解码数据队列，以数组方式实现)，渲染逻辑就会从pictq获取数据，同时解码逻辑又会往pictq写入数据
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex, pictq_windex;//队列长度，读/写位置索引
	SDL_mutex* pictq_lock;//队列读写锁对象，保护图像帧队列数据
	SDL_cond* pictq_ready;//队列就绪条件变量

	PacketQueue audioq;//音频编码数据包队列(编码数据队列，以链表方式实现)
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];//保存解码一个packet后的多帧原始音频数据(解码数据队列，以数组方式实现)
	unsigned int audio_buf_size;//解码后的多帧音频数据长度
	unsigned int audio_buf_index;//累计写入stream的长度
	uint8_t* audio_pkt_data;//编码数据缓存指针位置
	int audio_pkt_size;//缓存中剩余的编码数据长度(是否已完成一个完整的pakcet包的解码，一个数据包中可能包含多个音频编码帧)

	int videoStream, audioStream;//音视频流类型标号
	SDL_Thread* parse_tid;//编码数据包解析线程id
	SDL_Thread* decode_tid;//解码线程id

	char filename[1024];//输入文件完整路径名
	int quit;//全局退出进程标识，在界面上点了退出后，告诉线程退出
} VideoState;// Since we only have one decoding thread, the Big Struct can be global in case we need it.
VideoState* global_video_state;

//数据包队列初始化函数
void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));//全零初始化队列结构体对象
	q->qlock = SDL_CreateMutex();//创建互斥量对象
	q->qready = SDL_CreateCond();//创建条件变量对象
}

//向队列中插入数据包
int packet_queue_put(PacketQueue* q, AVPacket* pkt) {
	/*-------准备队列(链表)节点对象------*/
	AVPacketList* pktlist = (AVPacketList*)av_malloc(sizeof(AVPacketList));//在堆上创建链表节点对象
	if (!pktlist) {//检查链表节点对象是否创建成功
		return -1;
	}
	pktlist->pkt = *pkt;//将输入数据包赋值给新建链表节点对象中的数据包对象
	pktlist->next = NULL;//链表后继指针为空
//	if (av_packet_ref(pkt, pkt) < 0) {//增加pkt编码数据的引用计数(输入参数中的pkt与新建链表节点中的pkt共享同一缓存空间)
//		return -1;
//	}
/*---------将新建节点插入队列-------*/
	SDL_LockMutex(q->qlock);//队列互斥量加锁，保护队列数据

	if (!q->last_pkt) {//检查队列尾节点是否存在(检查队列是否为空)
		q->first_pkt = pktlist;//若不存在(队列尾空)，则将当前节点作队列为首节点
	}
	else {
		q->last_pkt->next = pktlist;//若已存在尾节点，则将当前节点挂到尾节点的后继指针上，并作为新的尾节点
	}
	q->last_pkt = pktlist;//将当前节点作为新的尾节点
	q->nb_packets++;//队列长度+1
	q->size += pktlist->pkt.size;//更新队列编码数据的缓存长度
	SDL_CondSignal(q->qready);//给等待线程发出消息，通知队列已就绪
	SDL_UnlockMutex(q->qlock);//释放互斥量
	return 0;
}

//从队列中提取数据包，并将提取的数据包出队列
static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block) {
	AVPacketList* pktlist;//临时链表节点对象指针
	int ret;//操作结果

	SDL_LockMutex(q->qlock);//队列互斥量加锁，保护队列数据
	for (;;) {
		if (global_video_state->quit) {//检查退出进程标识
			ret = -1;//操作失败
			break;
		}//end for if

		pktlist = q->first_pkt;//传递将队列首个数据包指针
		if (pktlist) {//检查数据包是否为空(队列是否有数据)
			q->first_pkt = pktlist->next;//队列首节点指针后移
			if (!q->first_pkt) {//检查首节点的后继节点是否存在
				q->last_pkt = NULL;//若不存在，则将尾节点指针置空
			}
			q->nb_packets--;//队列长度-1
			q->size -= pktlist->pkt.size;//更新队列编码数据的缓存长度
			*pkt = pktlist->pkt;//将队列首节点数据返回
			av_free(pktlist);//清空临时节点数据(清空首节点数据，首节点出队列)
			ret = 1;//操作成功
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {//队列处于未就绪状态，此时通过SDL_CondWait函数等待qready就绪信号，并暂时对互斥量解锁
		 /*---------------------
		  * 等待队列就绪信号qready，并对互斥量暂时解锁
		  * 此时线程处于阻塞状态，并置于等待条件就绪的线程列表上
		  * 使得该线程只在临界区资源就绪后才被唤醒，而不至于线程被频繁切换
		  * 该函数返回时，互斥量再次被锁住，并执行后续操作
		  --------------------*/
			SDL_CondWait(q->qready, q->qlock);//暂时解锁互斥量并将自己阻塞，等待临界区资源就绪(等待SDL_CondSignal发出临界区资源就绪的信号)
		}
	}//end for for-loop
	SDL_UnlockMutex(q->qlock);//释放互斥量
	return ret;
}
int queue_picture(VideoState* is, AVFrame* pFrame);
//视频解码线程函数
int decode_thread(void* arg) {
	VideoState* is = (VideoState*)arg;//传递用户数据
	AVPacket pkt, * packet = &pkt;//在栈上创建临时数据包对象并关联指针
	int frameFinished;//解码操作是否成功标识

	// Allocate video frame，为解码后的视频信息结构体分配空间并完成初始化操作(结构体中的图像缓存按照下面两步手动安装)
	AVFrame* pFrame = av_frame_alloc();

	for (;;) {
		if (packet_queue_get(&is->videoq, packet, 1) < 0) {//从队列中提取数据包到packet，并将提取的数据包出队列
			// Means we quit getting packets.
			break;
		}
		/*-----------------------
		 * Decode video frame，解码完整的一帧数据，并将frameFinished设置为true
		 * 可能无法通过只解码一个packet就获得一个完整的视频帧frame，可能需要读取多个packet才行
		 * avcodec_decode_video2()会在解码到完整的一帧时设置frameFinished为真
		 * Technically a packet can contain partial frames or other bits of data
		 * ffmpeg's parser ensures that the packets we get contain either complete or multiple frames
		 * convert the packet to a frame for us and set frameFinisned for us when we have the next frame
		 -----------------------*/
		avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished, packet);

		// Did we get a video frame，检查是否解码出完整一帧图像
		if (frameFinished) {
			if (queue_picture(is, pFrame) < 0) {//将解码完成的图像帧添加到图像帧队列
				break;
			}
		}
		av_packet_unref(packet);//释放pkt中保存的编码数据
	}
	av_free(pFrame);//清除pFrame中的内存空间
	return 0;
}

//音频解码函数，从缓存队列中提取数据包、解码，并返回解码后的数据长度(对一个完整的packet解码，将解码数据写入audio_buf缓存，并返回多帧解码数据的总长度)
int audio_decode_frame(VideoState* is) {
	int coded_consumed_size, data_size = 0;//每次消耗的编码数据长度[input](len1)，输出原始音频数据的缓存长度[output]
	AVPacket* pkt = &is->audio_pkt;//保存从队列中提取的数据包

	for (;;) {
		while (is->audio_pkt_size > 0) {//检查缓存中剩余的编码数据长度(是否已完成一个完整的pakcet包的解码，一个数据包中可能包含多个音频编码帧)
			int got_frame = 0;//解码操作成功标识，成功返回非零值
			//解码一帧音频数据，并返回消耗的编码数据长度
			coded_consumed_size = avcodec_decode_audio4(is->audio_st->codec, &is->audio_frame, &got_frame, pkt);
			if (coded_consumed_size < 0) {//检查是否执行了解码操作
				// If error, skip frame.
				is->audio_pkt_size = 0;//更新编码数据缓存长度
				break;
			}
			if (got_frame) {//检查解码操作是否成功
				//计算解码后音频数据长度[output]
				data_size = av_samples_get_buffer_size(NULL, is->audio_st->codec->channels, is->audio_frame.nb_samples, is->audio_st->codec->sample_fmt, 1);
				memcpy(is->audio_buf, is->audio_frame.data[0], data_size);//将解码数据复制到输出缓存
			}
			is->audio_pkt_data += coded_consumed_size;//更新编码数据缓存指针位置
			is->audio_pkt_size -= coded_consumed_size;//更新缓存中剩余的编码数据长度
			if (data_size <= 0) {//检查输出解码数据缓存长度
				// No data yet, get more frames.
				continue;
			}
			// We have data, return it and come back for more later.
			return data_size;//返回解码数据缓存长度
		}
		if (pkt->data) {//检查数据包是否已从队列中提取
			av_packet_unref(pkt);//释放pkt中保存的编码数据
		}

		if (is->quit) {//检查退出进程标识
			return -1;
		}
		// Next packet，从队列中提取数据包到pkt
		if (packet_queue_get(&is->audioq, pkt, 1) < 0) {
			return -1;
		}
		is->audio_pkt_data = pkt->data;//传递编码数据缓存指针
		is->audio_pkt_size = pkt->size;//传递编码数据缓存长度
	}
}

/*------Audio Callback-------
 * 音频输出回调函数，sdl通过该回调函数将解码后的pcm数据送入声卡播放,
 * sdl通常一次会准备一组缓存pcm数据，通过该回调送入声卡，声卡根据音频pts依次播放pcm数据
 * 待送入缓存的pcm数据完成播放后，再载入一组新的pcm缓存数据(每次音频输出缓存为空时，sdl就调用此函数填充音频输出缓存，并送入声卡播放)
 * When we begin playing audio, SDL will continually call this callback function
 * and ask it to fill the audio buffer with a certain number of bytes
 * The audio function callback takes the following parameters:
 * stream: A pointer to the audio buffer to be filled，输出音频数据到声卡缓存
 * len: The length (in bytes) of the audio buffer,缓存长度wanted_spec.samples=SDL_AUDIO_BUFFER_SIZE(1024)
 --------------------------*/
void audio_callback(void* userdata, Uint8* stream, int len) {
	VideoState* is = (VideoState*)userdata;//传递用户数据
	int wt_stream_len, audio_size;//每次写入stream的数据长度，解码后的数据长度

	while (len > 0) {//检查音频缓存的剩余长度
		if (is->audio_buf_index >= is->audio_buf_size) {//检查是否需要执行解码操作
			// We have already sent all our data; get more，从缓存队列中提取数据包、解码，并返回解码后的数据长度，audio_buf缓存中可能包含多帧解码后的音频数据
			audio_size = audio_decode_frame(is);
			if (audio_size < 0) {//检查解码操作是否成功
				// If error, output silence.
				is->audio_buf_size = 1024;
				memset(is->audio_buf, 0, is->audio_buf_size);//全零重置缓冲区
			}
			else {
				is->audio_buf_size = audio_size;//返回packet中包含的原始音频数据长度(多帧)
			}
			is->audio_buf_index = 0;//初始化累计写入缓存长度
		}//end for if

		wt_stream_len = is->audio_buf_size - is->audio_buf_index;//计算解码缓存剩余长度
		if (wt_stream_len > len) {//检查每次写入缓存的数据长度是否超过指定长度(1024)
			wt_stream_len = len;//指定长度从解码的缓存中取数据
		}

		//每次从解码的缓存数据中以指定长度抽取数据并写入stream传递给声卡
		memcpy(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, wt_stream_len);
		len -= wt_stream_len;//更新解码音频缓存的剩余长度
		stream += wt_stream_len;//更新缓存写入位置
		is->audio_buf_index += wt_stream_len;//更新累计写入缓存数据长度
	}//end for while
}

//根据指定类型打开流，找到对应的解码器、创建对应的音频配置、保存关键信息到 VideoState、启动音频和视频线程
int stream_component_open(VideoState* is, int stream_index) {
	AVFormatContext* pFormatCtx = is->pFormatCtx;//传递文件容器的封装信息及码流参数
	AVCodecContext* codecCtx = NULL;//解码器上下文对象，解码器依赖的相关环境、状态、资源以及参数集的接口指针
	AVCodec* codec = NULL;//保存编解码器信息的结构体，提供编码与解码的公共接口，可以看作是编码器与解码器的一个全局变量
	SDL_AudioSpec wanted_spec, spec;//SDL_AudioSpec a structure that contains the audio output format，创建 SDL_AudioSpec 结构体，设置音频播放数据
	AVDictionary* optionsDict = NULL;

	//检查输入的流类型是否在合理范围内
	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
		return -1;
	}

	// Get a pointer to the codec context for the video stream.
	codecCtx = pFormatCtx->streams[stream_index]->codec;//取得解码器上下文

	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {//检查解码器类型是否为音频解码器
		// Set audio settings from codec info,SDL_AudioSpec a structure that contains the audio output format
		// 创建SDL_AudioSpec结构体，设置音频播放参数
		wanted_spec.freq = codecCtx->sample_rate;//采样频率 DSP frequency -- samples per second
		wanted_spec.format = AUDIO_S16SYS;//采样格式 Audio data format
		wanted_spec.channels = codecCtx->channels;//声道数 Number of channels: 1 mono, 2 stereo
		wanted_spec.silence = 0;//无输出时是否静音
		//默认每次读音频缓存的大小，推荐值为 512~8192，ffplay使用的是1024 specifies a unit of audio data refers to the size of the audio buffer in sample frames
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;//设置读取音频数据的回调接口函数 the function to call when the audio device needs more data
		wanted_spec.userdata = is;//传递用户数据

		/*---------------------------
		 * 以指定参数打开音频设备，并返回与指定参数最为接近的参数，该参数为设备实际支持的音频参数
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
	 * Find the decoder for the video stream，根据视频流对应的解码器上下文查找对应的解码器，返回对应的解码器(信息结构体)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
	codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec || (avcodec_open2(codecCtx, codec, &optionsDict) < 0)) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}

	//检查解码器类型
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_AUDIO://音频解码器
		is->audioStream = stream_index;//音频流类型标号初始化
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_buf_size = 0;//解码后的多帧音频数据长度
		is->audio_buf_index = 0;//累计写入stream的长度
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);//音频数据包队列初始化
		SDL_PauseAudio(0);//audio callback starts running again，开启音频设备，如果这时候没有获得数据那么它就静音
		break;
	case AVMEDIA_TYPE_VIDEO://视频解码器
		is->videoStream = stream_index;//视频流类型标号初始化
		is->video_st = pFormatCtx->streams[stream_index];
		packet_queue_init(&is->videoq);//视频数据包队列初始化
		is->decode_tid = SDL_CreateThread(decode_thread, is);//创建解码线程
		// Initialize SWS context for software scaling，设置图像转换像素格式为AV_PIX_FMT_YUV420P
		is->sws_ctx = sws_getContext(is->video_st->codec->width, is->video_st->codec->height, is->video_st->codec->pix_fmt, is->video_st->codec->width, is->video_st->codec->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
		break;
	default:
		break;
	}
	return 0;
}

//视频(图像)帧渲染
void video_display(VideoState* is) {
	SDL_Rect rect;//SDL矩形对象
	VideoPicture* vp;//图像帧结构体指针
	float aspect_ratio;//宽度/高度比
	int w, h, x, y;//窗口尺寸及起始位置

	vp = &is->pictq[is->pictq_rindex];//从图像帧队列(数组)中提取图像帧结构对象
	if (vp->bmp) {//检查像素数据指针是否有效
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

		//设置矩形显示区域
		rect.x = x;
		rect.y = y;
		rect.w = w;
		rect.h = h;
		SDL_LockMutex(screen_lock);//锁定互斥量，保护画布的像素数据
		SDL_DisplayYUVOverlay(vp->bmp, &rect);//图像渲染
		SDL_UnlockMutex(screen_lock);//释放互斥锁
	}//end for if
}//end for video_display

//修改FFmpeg内部退出回调对应的函数
int decode_interrupt_cb(void* opaque) {
	return (global_video_state && global_video_state->quit);
}

//编码数据包解析线程函数(从视频文件中解析出音视频编码数据单元，一个AVPacket的data通常对应一个NAL)
int parse_thread(void* arg) {
	VideoState* is = (VideoState*)arg;//传递用户参数
	global_video_state = is;//传递全局状态参量结构体
	AVFormatContext* pFormatCtx = NULL;//保存文件容器封装信息及码流参数的结构体
	AVPacket pkt, * packet = &pkt;//在栈上创建临时数据包对象并关联指针

	// Find the first video/audio stream.
	is->videoStream = -1;//视频流类型标号初始化为-1
	is->audioStream = -1;//音频流类型标号初始化为-1
	int video_index = -1;//视频流类型标号初始化为-1
	int audio_index = -1;//音频流类型标号初始化为-1
	int i;//循环变量

	AVDictionary* io_dict = NULL;
	AVIOInterruptCB callback;
	// will interrupt blocking functions if we quit!.
	callback.callback = decode_interrupt_cb;
	callback.opaque = is;

	if (avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict)) {
		fprintf(stderr, "Unable to open I/O for %s\n", is->filename);
		return -1;
	}

	// Open video file，打开视频文件，取得文件容器的封装信息及码流参数
	if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0) {
		return -1; // Couldn't open file.
	}

	is->pFormatCtx = pFormatCtx;//传递文件容器封装信息及码流参数

	// Retrieve stream information，取得文件中保存的码流信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		return -1; // Couldn't find stream information.
	}

	// Dump information about file onto standard error，打印pFormatCtx中的码流信息
	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// Find the first video stream.
	for (i = 0; i < pFormatCtx->nb_streams; i++) {//遍历文件中包含的所有流媒体类型(视频流、音频流、字幕流等)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0) {//若文件中包含有视频流
			video_index = i;//用视频流类型的标号修改标识，使之不为-1
		}
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0) {//若文件中包含有音频流
			audio_index = i;//用音频流类型的标号修改标识，使之不为-1
		}
	}
	if (audio_index >= 0) {//检查文件中是否存在音频流
		stream_component_open(is, audio_index);//根据指定类型打开音频流
	}
	if (video_index >= 0) {//检查文件中是否存在视频流
		stream_component_open(is, video_index);//根据指定类型打开视频流
	}

	if (is->videoStream < 0 || is->audioStream < 0) {//检查文件中是否存在音视频流
		fprintf(stderr, "%s: could not open codecs\n", is->filename);
		goto fail;//跳转至异常处理
	}

	// Main decode loop.
	for (;;) {
		if (is->quit) {//检查退出进程标识
			break;
		}
		// Seek stuff goes here，检查音视频编码数据包队列长度是否溢出
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
		if (packet->stream_index == is->videoStream) {//检查数据包是否为视频类型
			packet_queue_put(&is->videoq, packet);//向队列中插入数据包
		}
		else if (packet->stream_index == is->audioStream) {//检查数据包是否为音频类型
			packet_queue_put(&is->audioq, packet);//向队列中插入数据包
		}
		else {//检查数据包是否为字幕类型
			av_packet_unref(packet);//释放packet中保存的(字幕)编码数据
		}
	}
	// All done - wait for it.
	while (!is->quit) {
		SDL_Delay(100);
	}

fail://异常处理
	if (1) {
		SDL_Event event;//SDL事件对象
		event.type = FF_QUIT_EVENT;//指定退出事件类型
		event.user.data1 = is;//传递用户数据
		SDL_PushEvent(&event);//将该事件对象压入SDL后台事件队列
	}
	return 0;
}

//创建/重置图像帧，为图像帧分配内存空间
void alloc_picture(void* userdata) {
	VideoState* is = (VideoState*)userdata;//传递用户数据
	VideoPicture* vp = &is->pictq[is->pictq_windex];//从图像帧队列(数组)中提取图像帧结构对象
	if (vp->bmp) {//检查图像帧是否已存在
		// We already have one make another, bigger/smaller.
		SDL_FreeYUVOverlay(vp->bmp);//释放当前overlay缓存
	}
	// Allocate a place to put our YUV image on that screen.
	SDL_LockMutex(screen_lock);//锁定互斥量，保护画布的像素数据
	//根据指定尺寸及像素格式重新创建像素缓存区
	vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width, is->video_st->codec->height, SDL_YV12_OVERLAY, screen);
	SDL_UnlockMutex(screen_lock);//释放互斥锁
	vp->width = is->video_st->codec->width;//设置图像帧宽度
	vp->height = is->video_st->codec->height;//设置图像帧高度

	SDL_LockMutex(is->pictq_lock);//锁定互斥量，保护画布的像素数据
	vp->allocated = 1;//图像帧像素缓冲区已分配内存
	SDL_CondSignal(is->pictq_ready);//给等待线程发出消息，通知队列已就绪
	SDL_UnlockMutex(is->pictq_lock);//释放互斥量
}

/*---------------------------
 * queue_picture：图像帧插入队列等待渲染
 * @is：全局状态参数集
 * @pFrame：保存图像解码数据的结构体
 * 1、首先检查图像帧队列(数组)是否存在空间插入新的图像，若没有足够的空间插入图像则使当前线程休眠等待
 * 2、在初始化的条件下，队列(数组)中VideoPicture的bmp对象(YUV overlay)尚未分配空间，通过FF_ALLOC_EVENT事件的方法调用alloc_picture分配空间
 * 3、当队列(数组)中所有VideoPicture的bmp对象(YUV overlay)均已分配空间的情况下，直接跳过步骤2向bmp对象拷贝像素数据，像素数据在进行格式转换后执行拷贝操作
 ---------------------------*/
int queue_picture(VideoState* is, AVFrame* pFrame) {
	/*--------1、检查队列是否有插入空间-------*/
		// Wait until we have space for a new pic.
	SDL_LockMutex(is->pictq_lock);//锁定互斥量，保护图像帧队列
	while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {//检查队列当前长度
		SDL_CondWait(is->pictq_ready, is->pictq_lock);//线程休眠等待pictq_ready信号
	}
	SDL_UnlockMutex(is->pictq_lock);//释放互斥量

	if (is->quit) {//检查进程退出标识
		return -1;
	}
	/*-------2、初始化/重置YUV overlay-------*/
		// windex is set to 0 initially.
	VideoPicture* vp = &is->pictq[is->pictq_windex];//从图像帧队列中抽取图像帧对象

	// Allocate or resize the buffer，检查YUV overlay是否已存在，否则初始化YUV overlay，分配像素缓存空间
	if (!vp->bmp || vp->width != is->video_st->codec->width || vp->height != is->video_st->codec->height) {
		SDL_Event event;//SDL事件对象

		vp->allocated = 0;//图像帧未分配空间
		// We have to do it in the main thread.
		event.type = FF_ALLOC_EVENT;//指定分配图像帧内存事件
		event.user.data1 = is;//传递用户数据
		SDL_PushEvent(&event);//发送SDL事件

		// Wait until we have a picture allocated.
		SDL_LockMutex(is->pictq_lock);//锁定互斥量，保护图像帧队列
		while (!vp->allocated && !is->quit) {//检查当前图像帧是否已初始化(为SDL overlay)
			SDL_CondWait(is->pictq_ready, is->pictq_lock);//线程休眠等待alloc_picture发送pictq_ready信号唤醒当前线程
		}
		SDL_UnlockMutex(is->pictq_lock);//释放互斥量
		if (is->quit) {//检查进程退出标识
			return -1;
		}
	}//end for if
/*--------3、拷贝视频帧到YUV overlay-------*/
	AVFrame pict;//临时保存转换后的图像帧像素，与队列中的元素相关联
	// We have a place to put our picture on the queue.
	if (vp->bmp) {//检查像素数据指针是否有效
		SDL_LockYUVOverlay(vp->bmp);//locks the overlay for direct access to pixel data，原子操作，保护像素缓冲区，避免非法修改

		// Point pict at the queue.
		pict.data[0] = vp->bmp->pixels[0];//将转码后的图像与画布的像素缓冲器关联
		pict.data[1] = vp->bmp->pixels[2];
		pict.data[2] = vp->bmp->pixels[1];

		pict.linesize[0] = vp->bmp->pitches[0];//将转码后的图像扫描行长度与画布像素缓冲区的扫描行长度相关联
		pict.linesize[1] = vp->bmp->pitches[2];//linesize-Size, in bytes, of the data for each picture/channel plane
		pict.linesize[2] = vp->bmp->pitches[1];//For audio, only linesize[0] may be set

		// Convert the image into YUV format that SDL uses，将解码后的图像帧转换为AV_PIX_FMT_YUV420P格式，并拷贝到图像帧队列
		sws_scale(is->sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, is->video_st->codec->height, pict.data, pict.linesize);

		SDL_UnlockYUVOverlay(vp->bmp);//Unlocks a previously locked overlay. An overlay must be unlocked before it can be displayed
		// Now we inform our display thread that we have a pic ready.
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {//更新并检查当前图像帧队列写入位置
			is->pictq_windex = 0;//重置图像帧队列写入位置
		}
		SDL_LockMutex(is->pictq_lock);//锁定队列读写锁，保护队列数据
		is->pictq_size++;//更新图像帧队列长度
		SDL_UnlockMutex(is->pictq_lock);//释放队列读写锁
	}//end for if
	return 0;
}

//定时器触发的回调函数
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque) {
	SDL_Event event;//SDL事件对象
	event.type = FF_REFRESH_EVENT;//视频显示刷新事件
	event.user.data1 = opaque;//传递用户数据
	SDL_PushEvent(&event);//发送事件
	return 0; // 0 means stop timer.
}

/*---------------------------
 * Schedule a video refresh in 'delay' ms.
 * 告诉sdl在指定的延时后来推送一个 FF_REFRESH_EVENT 事件
 * 这个事件将在事件队列里触发sdl_refresh_timer_cb函数的调用
 --------------------------*/
static void schedule_refresh(VideoState* is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);//在指定的时间(ms)后回调用户指定的函数
}

//显示刷新函数(FF_REFRESH_EVENT响应函数)
void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;//传递用户数据
	// vp is used in later tutorials for synchronization.
	if (is->video_st) {
		if (is->pictq_size == 0) {//检查图像帧队列是否有待显示图像
			schedule_refresh(is, 1);
		}
		else {//刷新图像
		 /*-------------------------
		  * Now, normally here goes a ton of code about timing, etc.
		  * we're just going to guess at a delay for now.
		  * You can increase and decrease this value and hard code the timing
		  * but I don't suggest that ;) We'll learn how to do it for real later..
		  ------------------------*/
			schedule_refresh(is, 40);//设置显示下一帧图像的刷新时间，通过定时器timer方式触发
			// Show the picture!
			video_display(is);//图像帧渲染
			// Update queue for next picture!
			if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {//更新并检查图像帧队列读位置索引
				is->pictq_rindex = 0;//重置读位置索引
			}
			SDL_LockMutex(is->pictq_lock);//锁定互斥量，保护画布的像素数据
			is->pictq_size--;//更新图像帧队列长度
			SDL_CondSignal(is->pictq_ready);//发送队列就绪信号
			SDL_UnlockMutex(is->pictq_lock);//释放互斥量
		}
	}
	else {
		schedule_refresh(is, 100);
	}
}

int main(int argc, char* argv[]) {
	//if (argc < 2) {//检查输入参数个数是否正确
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}
	// Register all formats and codecs，注册所有支持的多媒体格式及编解码器
	av_register_all();

	VideoState* is = (VideoState*)av_mallocz(sizeof(VideoState));//创建全局状态对象
	av_strlcpy(is->filename, fileName, sizeof(is->filename));//复制视频文件路径名
	screen_lock = SDL_CreateMutex();//创建像素缓冲区锁对象
	is->pictq_lock = SDL_CreateMutex();//创建编码数据包队列互斥锁对象
	is->pictq_ready = SDL_CreateCond();//创建编码数据包队列就绪条件对象

	//SDL_Init initialize the Event Handling, File I/O, and Threading subsystems，初始化SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());//initialize the video audio & timer subsystem
		exit(1);
	}
	// Make a screen to put our video,在SDL2.0中SDL_SetVideoMode及SDL_Overlay已经弃用，改为SDL_CreateWindow及SDL_CreateRenderer创建窗口及着色器
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(640, 480, 0, 0);//创建SDL窗口及绘图表面，并指定图像尺寸及像素格式
#else
	screen = SDL_SetVideoMode(640, 480, 24, 0);//创建SDL窗口及绘图表面，并指定图像尺寸及像素格式
#endif
	if (!screen) {//检查SDL(绘图表面)窗口是否创建成功(SDL用绘图表面对象操作窗口)
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}

	schedule_refresh(is, 40);//在指定的时间(40ms)后回调用户指定的函数，进行图像帧的显示更新

	is->parse_tid = SDL_CreateThread(parse_thread, is);//创建编码数据包解析线程
	if (!is->parse_tid) {//检查线程是否创建成功
		av_free(is);
		return -1;
	}

	SDL_Event event;//SDL事件对象
	for (;;) {//SDL事件循环
		SDL_WaitEvent(&event);//Use this function to wait indefinitely for the next available event，主线程阻塞，等待事件到来
		switch (event.type) {//事件到来后唤醒主线程，检查事件类型
		case FF_QUIT_EVENT:
		case SDL_QUIT://退出进程事件
			is->quit = 1;
			// If the video has finished playing, then both the picture and audio queues are waiting for more data.  
			// Make them stop waiting and terminate normally..
			SDL_CondSignal(is->audioq.qready);//发出队列就绪信号避免死锁
			SDL_CondSignal(is->videoq.qready);
			SDL_Quit();
			return 0;
			break;
		case FF_ALLOC_EVENT://分配overlay事件
			alloc_picture(event.user.data1);//分配overlay事件响应函数
			break;
		case FF_REFRESH_EVENT://视频显示刷新事件
			video_refresh_timer(event.user.data1);//视频显示刷新事件响应函数
			break;
		default:
			break;
		}
	}
	system("pause");
	return 0;
}

