// tutorial01.c
// Code based on a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101 
// on GCC 4.7.2 in Debian February 2015
//
// Updates tested on:
// Mac OS X 10.11.6
// Apple LLVM version 8.0.0 (clang-800.0.38)
//
// A small sample program that shows how to use libavformat and libavcodec to read video from a file.
//
// Use
//
// $ gcc -o tutorial01 tutorial01.c -lavutil -lavformat -lavcodec -lswscale -lz -lm
//
// to build (assuming libavutil/libavformat/libavcodec/libswscale are correctly installed your system).
//
// Run using
//
// $ tutorial01 myvideofile.mpg
//
// to write the first five frames from "myvideofile.mpg" to disk in PPM format.

// comment by breakpointlab@outlook.com

extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
	#include "libavutil/imgutils.h"
}
//#undef main
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avdevice.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"postproc.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#include <stdio.h>
const char* fileName = "../data/test.mp4";

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

//����PPM�ļ�
void SaveFrame(AVFrame* pFrame, int width, int height, int iFrame) {
	FILE* pFile;//�����ļ�����
	char szFilename[32];//��������ļ���

	// Open file�����ļ�
	sprintf(szFilename, "../data/frame%d.ppm", iFrame);//��ʽ������ļ���
	pFile = fopen(szFilename, "wb");//������ļ�
	if (pFile == NULL) {//�������ļ��Ƿ�򿪳ɹ�
		return;
	}

	// Write header indicated how wide & tall the image is��������ļ���д���ļ�ͷ
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Write pixel data��write the file one line a time,һ��һ��ѭ��д��RGB24����ֵ
	int y;
	for (y = 0; y < height; y++) {
		fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
	}

	// Close file���ر��ļ�
	fclose(pFile);
}

int main(int argc, char* argv[]) {
	/*--------------��������-------------*/
		// Initalizing these to NULL prevents segfaults!
	AVFormatContext* pFormatCtx = NULL;//�����ļ�������װ��Ϣ�����������Ľṹ��
	AVCodecContext* pCodecCtxOrig = NULL;//�����������Ķ��󣬽�������������ػ�����״̬����Դ�Լ��������Ľӿ�ָ��
	AVCodecContext* pCodecCtx = NULL;//�����������Ķ���,����PPM�ļ����
	AVCodec* pCodec = NULL;//������������Ϣ�Ľṹ�壬�ṩ���������Ĺ����ӿڣ����Կ����Ǳ��������������һ��ȫ�ֱ���
	AVPacket packet;//���𱣴�ѹ���������������Ϣ�Ľṹ��,ÿ֡ͼ����һ�����packet�����
	AVFrame* pFrame = NULL;//��������Ƶ���������ݣ���״̬��Ϣ�����������Ϣ��������ͱ�QP���˶�ʸ���������
	AVFrame* pFrameRGB = NULL;//�������24-bit RGB��PPM�ļ�����
	struct SwsContext* sws_ctx = NULL;//����ת���������Ľṹ��

	int numBytes;//RGB24��ʽ���ݳ���
	uint8_t* buffer = NULL;//���������������ָ��
	int i, videoStream;//ѭ����������Ƶ�����ͱ��
	int frameFinished;//��������Ƿ�ɹ���ʶ

/*-------------������ʼ��------------*/
	//if (argc < 2) {//���������������Ƿ���ȷ
	//	fprintf(stderr, "Please provide a movie file\n");
	//	system("pause");
	//	//printf("Please provide a movie file\n");
	//	return -1;
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
			break;//�˳�ѭ��
		}
	}
	if (videoStream == -1) {//����ļ����Ƿ������Ƶ��
		return -1; // Didn't find a video stream.
	}

	// Get a pointer to the codec context for the video stream�����������ͱ�Ŵ�pFormatCtx->streams��ȡ����Ƶ����Ӧ�Ľ�����������
	pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;
	/*-----------------------
	 * Find the decoder for the video stream��������Ƶ����Ӧ�Ľ����������Ĳ��Ҷ�Ӧ�Ľ����������ض�Ӧ�Ľ�����(��Ϣ�ṹ��)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if (pCodec == NULL) {//���������Ƿ�ƥ��
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found.
	}

	// Copy context�����Ʊ�����������Ķ������ڱ������Ƶ���г�ȡ��֡
	pCodecCtx = avcodec_alloc_context3(pCodec);//����AVCodecContext�ṹ�����pCodecCtx
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {//���Ʊ�����������Ķ���
		fprintf(stderr, "Couldn't copy codec context");
		return -1; // Error copying codec context.
	}

	// Open codec���򿪽�����
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		return -1; // Could not open codec.
	}

	// Allocate video frame��Ϊ��������Ƶ��Ϣ�ṹ�����ռ䲢��ɳ�ʼ������(�ṹ���е�ͼ�񻺴水�����������ֶ���װ)
	pFrame = av_frame_alloc();

	// Allocate an AVFrame structure��Ϊת��PPM�ļ��Ľṹ�����ռ䲢��ɳ�ʼ������
	pFrameRGB = av_frame_alloc();
	if (pFrameRGB == NULL) {//����ʼ�������Ƿ�ɹ�
		return -1;
	}

	// Determine required buffer size and allocate buffer���������ظ�ʽ��ͼ��ߴ�����ڴ��С
	numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));//Ϊת�����RGB24ͼ�����û���ռ�

	// Assign appropriate parts of buffer to image planes in pFrameRGB Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
	// ΪAVFrame����װͼ�񻺴棬��out_buffer����ҵ�pFrameYUV->dataָ��ṹ��
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

	// Initialize SWS context for software scaling������ͼ��ת�����ظ�ʽΪAV_PIX_FMT_RGB24
	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

	/*--------------ѭ������-------------*/
	i = 0;// Read frames(2 packet) and save first five frames to disk,
	/*-----------------------
	 * read in a packet and store it in the AVPacket struct
	 * ffmpeg allocates the internal data for us,which is pointed to by packet.data
	 * this is freed by the av_free_packet()
	 -----------------------*/
	while (av_read_frame(pFormatCtx, &packet) >= 0) {//����Ƶ�ļ���������ý�������ζ�ȡÿ��ͼ��������ݰ������洢��AVPacket���ݽṹ��
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
				// Convert the image from its native format to RGB��//��������ͼ��ת��ΪRGB24��ʽ
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

				if (++i <= 5) {// Save the frame to disk����ǰ5֡ͼ��洢��������
					SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
				}
			}
		}
		// Free the packet that was allocated by av_read_frame���ͷ�AVPacket���ݽṹ�б�������ָ��
		av_packet_unref(&packet);
	}

	/*--------------��������-------------*/
		// Free the RGB image buffer
	av_free(buffer);
	av_frame_free(&pFrameRGB);

	// Free the YUV frame.
	av_frame_free(&pFrame);

	// Close the codecs.
	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxOrig);

	// Close the video file.
	avformat_close_input(&pFormatCtx);
	system("pause");
	return 0;
}
