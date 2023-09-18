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
	/*--------------��������-------------*/
	AVFormatContext* pFormatCtx = NULL;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVCodecContext* pCodecCtx = NULL;//�����������Ķ��󣬽�������������ػ�����״̬����Դ�Լ��������Ľӿ�ָ��
	AVCodec* pCodec = NULL;//������������Ϣ�Ľṹ�壬�ṩ���������Ĺ����ӿڣ����Կ����Ǳ��������������һ��ȫ�ֱ���
	AVPacket packet;//���𱣴�ѹ���������������Ϣ�Ľṹ��,ÿ֡ͼ����һ�����packet�����
	AVFrame* pFrame = NULL;//��������Ƶ���������ݣ���״̬��Ϣ�����������Ϣ��������ͱ�QP���˶�ʸ���������
	struct SwsContext* sws_ctx = NULL;//����ת���������Ľṹ��
	AVDictionary* optionsDict = NULL;

	SDL_Surface* screen = NULL;//SDL��ͼ���ڣ�A structure that contains a collection of pixels used in software blitting
	SDL_Overlay* bmp = NULL;//SDL����
	SDL_Rect rect;//SDL���ζ���
	SDL_Event event;//SDL�¼�����

	int i, videoStream;//ѭ����������Ƶ�����ͱ��
	int frameFinished;//��������Ƿ�ɹ���ʶ

/*-------------������ʼ��------------*/
	//if (argc < 2) {//���������������Ƿ���ȷ
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}
	// Register all available formats and codecs��ע������ffmpeg֧�ֵĶ�ý���ʽ���������
	av_register_all();

	/*-----------------------
	 * Open video file������Ƶ�ļ������ļ�ͷ���ݣ�ȡ���ļ������ķ�װ��Ϣ�������������洢��pFormatCtx��
	 * read the file header and stores information about the file format in the AVFormatContext structure
	 * The last three arguments are used to specify the file format, buffer size, and format options
	 * but by setting this to NULL or 0, libavformat will auto-detect these
	 -----------------------*/
	if (avformat_open_input(&pFormatCtx, fileName, NULL, NULL) != 0) {
		return -1; // Couldn't open file.
	}

	/*-----------------------
	 * ȡ���ļ��б����������Ϣ������䵽pFormatCtx->stream �ֶ�
	 * check out & Retrieve the stream information in the file
	 * then populate pFormatCtx->stream with the proper information
	 * pFormatCtx->streams is just an array of pointers, of size pFormatCtx->nb_streams
	 -----------------------*/
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		return -1; // Couldn't find stream information.
	}

	// Dump information about file onto standard error����ӡpFormatCtx�е�������Ϣ
	av_dump_format(pFormatCtx, 0, fileName, 0);

	// Find the first video stream.
	videoStream = -1;//��Ƶ�����ͱ�ų�ʼ��Ϊ-1
	for (i = 0; i < pFormatCtx->nb_streams; i++) {//�����ļ��а�����������ý������(��Ƶ������Ƶ������Ļ����)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {//���ļ��а�������Ƶ��
			videoStream = i;//����Ƶ�����͵ı���޸ı�ʶ��ʹ֮��Ϊ-1
			break;
		}
	}
	if (videoStream == -1) {//����ļ����Ƿ������Ƶ��
		return -1; // Didn't find a video stream.
	}

	// Get a pointer to the codec context for the video stream�����������ͱ�Ŵ�pFormatCtx->streams��ȡ����Ƶ����Ӧ�Ľ�����������
	pCodecCtx = pFormatCtx->streams[videoStream]->codec;

	/*-----------------------
	 * Find the decoder for the video stream��������Ƶ����Ӧ�Ľ����������Ĳ��Ҷ�Ӧ�Ľ����������ض�Ӧ�Ľ�����(��Ϣ�ṹ��)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {//���������Ƿ�ƥ��
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found.
	}

	// Open codec���򿪽�����
	if (avcodec_open2(pCodecCtx, pCodec, &optionsDict) < 0) {
		return -1; // Could not open codec.
	}

	// Allocate video frame��Ϊ��������Ƶ��Ϣ�ṹ�����ռ䲢��ɳ�ʼ������(�ṹ���е�ͼ�񻺴水�����������ֶ���װ)
	pFrame = av_frame_alloc();

	// Initialize SWS context for software scaling������ͼ��ת�����ظ�ʽΪAV_PIX_FMT_YUV420P
	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

	//SDL_Init initialize the Event Handling, File I/O, and Threading subsystems����ʼ��SDL 
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {//initialize the video audio & timer subsystem 
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());//tell the library what features we're going to use
		exit(1);
	}

	// Make a screen to put our video,��SDL2.0��SDL_SetVideoMode��SDL_Overlay�Ѿ����ã���ΪSDL_CreateWindow��SDL_CreateRenderer�������ڼ���ɫ��
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);//����SDL���ڣ���ָ��ͼ��ߴ�
#else
	screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);//����SDL���ڣ���ָ��ͼ��ߴ�
#endif
	if (!screen) {//���SDL�����Ƿ񴴽��ɹ�
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}
	SDL_WM_SetCaption(argv[1], 0);//�������ļ�������SDL���ڱ���

	// Allocate a place to put our YUV image on that screen��������������
	bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height, SDL_YV12_OVERLAY, screen);

	/*--------------ѭ������-------------*/
	i = 0;// Read frames and save first five frames to disk
	/*-----------------------
	 * read in a packet and store it in the AVPacket struct
	 * ffmpeg allocates the internal data for us,which is pointed to by packet.data
	 * this is freed by the av_free_packet()
	 -----------------------*/
	while (av_read_frame(pFormatCtx, &packet) >= 0) {//���ļ������ζ�ȡÿ��ͼ��������ݰ������洢��AVPacket���ݽṹ��
		// Is this a packet from the video stream��������ݰ�����
		if (packet.stream_index == videoStream) {
			/*-----------------------
			 * Decode video frame������������һ֡���ݣ�����frameFinished����Ϊtrue
			 * �����޷�ͨ��ֻ����һ��packet�ͻ��һ����������Ƶ֡frame��������Ҫ��ȡ���packet����
			 * avcodec_decode_video2()���ڽ��뵽������һ֡ʱ����frameFinishedΪ��
			 * Technically a packet can contain partial frames or other bits of data
			 * ffmpeg's parser ensures that the packets we get contain either complete or multiple frames
			 * convert the packet to a frame for us and set frameFinisned for us when we have the next frame
			 -----------------------*/
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			// Did we get a video frame������Ƿ���������һ֡ͼ��
			if (frameFinished) {
				SDL_LockYUVOverlay(bmp);//locks the overlay for direct access to pixel data��ԭ�Ӳ������������ػ������ٽ���Դ

				AVFrame pict;//����ת��ΪAV_PIX_FMT_YUV420P��ʽ����Ƶ֡
				pict.data[0] = bmp->pixels[0];//��ת����ͼ���뻭�������ػ���������
				pict.data[1] = bmp->pixels[2];
				pict.data[2] = bmp->pixels[1];

				pict.linesize[0] = bmp->pitches[0];//��ת����ͼ��ɨ���г����뻭�����ػ�������ɨ���г��������
				pict.linesize[1] = bmp->pitches[2];//linesize-Size, in bytes, of the data for each picture/channel plane
				pict.linesize[2] = bmp->pitches[1];//For audio, only linesize[0] may be set

				// Convert the image into YUV format that SDL uses����������ͼ��ת��ΪAV_PIX_FMT_YUV420P��ʽ������ֵ��pict����
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict.data, pict.linesize);

				SDL_UnlockYUVOverlay(bmp);//Unlocks a previously locked overlay. An overlay must be unlocked before it can be displayed

				//���þ�����ʾ����
				rect.x = 0;
				rect.y = 0;
				rect.w = pCodecCtx->width;
				rect.h = pCodecCtx->height;
				SDL_DisplayYUVOverlay(bmp, &rect);//ͼ����Ⱦ
			}
		}

		// Free the packet that was allocated by av_read_frame���ͷ�AVPacket���ݽṹ�б�������ָ��
		av_packet_unref(&packet);

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
			printf("SDL_QUIT\n");
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
	avcodec_close(pCodecCtx);

	// Close the video file.
	avformat_close_input(&pFormatCtx);
	system("pause");
	return 0;
}

