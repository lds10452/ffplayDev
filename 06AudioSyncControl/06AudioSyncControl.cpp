// tutorial02.c
// A pedagogical video player that will stream through every video frame as fast as it can.
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
// $ gcc -o tutorial02 tutorial02.c -lavutil -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
//
// to build (assuming libavutil/libavformat/libavcodec/libswscale are correctly installed your system).
//
// Run using
//
// $ tutorial02 myvideofile.mpg
//
// to play the video stream on your screen.

#include "SDL.h"
extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
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

int main(int argc, char* argv[]) {
	/*--------------参数定义-------------*/
	AVFormatContext* pFormatCtx = NULL;//保存文件容器封装信息及码流参数的结构体
	AVCodecContext* pCodecCtx = NULL;//解码器上下文对象，解码器依赖的相关环境、状态、资源以及参数集的接口指针
	AVCodec* pCodec = NULL;//保存编解码器信息的结构体，提供编码与解码的公共接口，可以看作是编码器与解码器的一个全局变量
	AVPacket packet;//负责保存压缩编码数据相关信息的结构体,每帧图像由一到多个packet包组成
	AVFrame* pFrame = NULL;//保存音视频解码后的数据，如状态信息、编解码器信息、宏块类型表，QP表，运动矢量表等数据
	struct SwsContext* sws_ctx = NULL;//描述转换器参数的结构体
	AVDictionary* optionsDict = NULL;

	SDL_Surface* screen = NULL;//SDL绘图窗口，A structure that contains a collection of pixels used in software blitting
	SDL_Overlay* bmp = NULL;//SDL画布
	SDL_Rect rect;//SDL矩形对象
	SDL_Event event;//SDL事件对象

	int i, videoStream;//循环变量，视频流类型标号
	int frameFinished;//解码操作是否成功标识

/*-------------参数初始化------------*/
	//if (argc < 2) {//检查输入参数个数是否正确
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}
	// Register all available formats and codecs，注册所有ffmpeg支持的多媒体格式及编解码器
	av_register_all();

	/*-----------------------
	 * Open video file，打开视频文件，读文件头内容，取得文件容器的封装信息及码流参数并存储在pFormatCtx中
	 * read the file header and stores information about the file format in the AVFormatContext structure
	 * The last three arguments are used to specify the file format, buffer size, and format options
	 * but by setting this to NULL or 0, libavformat will auto-detect these
	 -----------------------*/
	if (avformat_open_input(&pFormatCtx, fileName, NULL, NULL) != 0) {
		return -1; // Couldn't open file.
	}

	/*-----------------------
	 * 取得文件中保存的码流信息，并填充到pFormatCtx->stream 字段
	 * check out & Retrieve the stream information in the file
	 * then populate pFormatCtx->stream with the proper information
	 * pFormatCtx->streams is just an array of pointers, of size pFormatCtx->nb_streams
	 -----------------------*/
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		return -1; // Couldn't find stream information.
	}

	// Dump information about file onto standard error，打印pFormatCtx中的码流信息
	av_dump_format(pFormatCtx, 0, fileName, 0);

	// Find the first video stream.
	videoStream = -1;//视频流类型标号初始化为-1
	for (i = 0; i < pFormatCtx->nb_streams; i++) {//遍历文件中包含的所有流媒体类型(视频流、音频流、字幕流等)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {//若文件中包含有视频流
			videoStream = i;//用视频流类型的标号修改标识，使之不为-1
			break;
		}
	}
	if (videoStream == -1) {//检查文件中是否存在视频流
		return -1; // Didn't find a video stream.
	}

	// Get a pointer to the codec context for the video stream，根据流类型标号从pFormatCtx->streams中取得视频流对应的解码器上下文
	pCodecCtx = pFormatCtx->streams[videoStream]->codec;

	/*-----------------------
	 * Find the decoder for the video stream，根据视频流对应的解码器上下文查找对应的解码器，返回对应的解码器(信息结构体)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {//检查解码器是否匹配
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found.
	}

	// Open codec，打开解码器
	if (avcodec_open2(pCodecCtx, pCodec, &optionsDict) < 0) {
		return -1; // Could not open codec.
	}

	// Allocate video frame，为解码后的视频信息结构体分配空间并完成初始化操作(结构体中的图像缓存按照下面两步手动安装)
	pFrame = av_frame_alloc();

	// Initialize SWS context for software scaling，设置图像转换像素格式为AV_PIX_FMT_YUV420P
	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

	//SDL_Init initialize the Event Handling, File I/O, and Threading subsystems，初始化SDL 
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {//initialize the video audio & timer subsystem 
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());//tell the library what features we're going to use
		exit(1);
	}

	// Make a screen to put our video,在SDL2.0中SDL_SetVideoMode及SDL_Overlay已经弃用，改为SDL_CreateWindow及SDL_CreateRenderer创建窗口及着色器
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);//创建SDL窗口，并指定图像尺寸
#else
	screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);//创建SDL窗口，并指定图像尺寸
#endif
	if (!screen) {//检查SDL窗口是否创建成功
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}
	SDL_WM_SetCaption(argv[1], 0);//用输入文件名设置SDL窗口标题

	// Allocate a place to put our YUV image on that screen，创建画布对象
	bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height, SDL_YV12_OVERLAY, screen);

	/*--------------循环解码-------------*/
	i = 0;// Read frames and save first five frames to disk
	/*-----------------------
	 * read in a packet and store it in the AVPacket struct
	 * ffmpeg allocates the internal data for us,which is pointed to by packet.data
	 * this is freed by the av_free_packet()
	 -----------------------*/
	while (av_read_frame(pFormatCtx, &packet) >= 0) {//从文件中依次读取每个图像编码数据包，并存储在AVPacket数据结构中
		// Is this a packet from the video stream，检查数据包类型
		if (packet.stream_index == videoStream) {
			/*-----------------------
			 * Decode video frame，解码完整的一帧数据，并将frameFinished设置为true
			 * 可能无法通过只解码一个packet就获得一个完整的视频帧frame，可能需要读取多个packet才行
			 * avcodec_decode_video2()会在解码到完整的一帧时设置frameFinished为真
			 * Technically a packet can contain partial frames or other bits of data
			 * ffmpeg's parser ensures that the packets we get contain either complete or multiple frames
			 * convert the packet to a frame for us and set frameFinisned for us when we have the next frame
			 -----------------------*/
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			// Did we get a video frame，检查是否解码出完整一帧图像
			if (frameFinished) {
				SDL_LockYUVOverlay(bmp);//locks the overlay for direct access to pixel data，原子操作，保护像素缓冲区临界资源

				AVFrame pict;//保存转换为AV_PIX_FMT_YUV420P格式的视频帧
				pict.data[0] = bmp->pixels[0];//将转码后的图像与画布的像素缓冲器关联
				pict.data[1] = bmp->pixels[2];
				pict.data[2] = bmp->pixels[1];

				pict.linesize[0] = bmp->pitches[0];//将转码后的图像扫描行长度与画布像素缓冲区的扫描行长度相关联
				pict.linesize[1] = bmp->pitches[2];//linesize-Size, in bytes, of the data for each picture/channel plane
				pict.linesize[2] = bmp->pitches[1];//For audio, only linesize[0] may be set

				// Convert the image into YUV format that SDL uses，将解码后的图像转换为AV_PIX_FMT_YUV420P格式，并赋值到pict对象
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict.data, pict.linesize);

				SDL_UnlockYUVOverlay(bmp);//Unlocks a previously locked overlay. An overlay must be unlocked before it can be displayed

				//设置矩形显示区域
				rect.x = 0;
				rect.y = 0;
				rect.w = pCodecCtx->width;
				rect.h = pCodecCtx->height;
				SDL_DisplayYUVOverlay(bmp, &rect);//图像渲染
			}
		}

		// Free the packet that was allocated by av_read_frame，释放AVPacket数据结构中编码数据指针
		av_packet_unref(&packet);

		/*-------------------------
		 * 在每次循环中从SDL后台队列取事件并填充到SDL_Event对象中
		 * SDL的事件系统使得你可以接收用户的输入，从而完成一些控制操作
		 * SDL_PollEvent() is the favored way of receiving system events
		 * since it can be done from the main loop and does not suspend the main loop
		 * while waiting on an event to be posted
		 * poll for events right after we finish processing a packet
		 ------------------------*/
		SDL_PollEvent(&event);
		switch (event.type) {//检查SDL事件对象
		case SDL_QUIT://退出事件
			printf("SDL_QUIT\n");
			SDL_Quit();//退出操作
			exit(0);//结束进程
			break;
		default:
			break;
		}//end for switch
	}//end for while
/*--------------参数撤销-------------*/
	// Free the YUV frame.
	av_free(pFrame);

	// Close the codec.
	avcodec_close(pCodecCtx);

	// Close the video file.
	avformat_close_input(&pFormatCtx);
	system("pause");
	return 0;
}

