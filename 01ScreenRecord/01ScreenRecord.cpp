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

//保存PPM文件
void SaveFrame(AVFrame* pFrame, int width, int height, int iFrame) {
	FILE* pFile;//定义文件对象
	char szFilename[32];//定义输出文件名

	// Open file，打开文件
	sprintf(szFilename, "../data/frame%d.ppm", iFrame);//格式化输出文件名
	pFile = fopen(szFilename, "wb");//打开输出文件
	if (pFile == NULL) {//检查输出文件是否打开成功
		return;
	}

	// Write header indicated how wide & tall the image is，向输出文件中写入文件头
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Write pixel data，write the file one line a time,一次一行循环写入RGB24像素值
	int y;
	for (y = 0; y < height; y++) {
		fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
	}

	// Close file，关闭文件
	fclose(pFile);
}

int main(int argc, char* argv[]) {
	/*--------------参数定义-------------*/
		// Initalizing these to NULL prevents segfaults!
	AVFormatContext* pFormatCtx = NULL;//保存文件容器封装信息及码流参数的结构体
	AVCodecContext* pCodecCtxOrig = NULL;//解码器上下文对象，解码器依赖的相关环境、状态、资源以及参数集的接口指针
	AVCodecContext* pCodecCtx = NULL;//编码器上下文对象,用于PPM文件输出
	AVCodec* pCodec = NULL;//保存编解码器信息的结构体，提供编码与解码的公共接口，可以看作是编码器与解码器的一个全局变量
	AVPacket packet;//负责保存压缩编码数据相关信息的结构体,每帧图像由一到多个packet包组成
	AVFrame* pFrame = NULL;//保存音视频解码后的数据，如状态信息、编解码器信息、宏块类型表，QP表，运动矢量表等数据
	AVFrame* pFrameRGB = NULL;//保存输出24-bit RGB的PPM文件数据
	struct SwsContext* sws_ctx = NULL;//描述转换器参数的结构体

	int numBytes;//RGB24格式数据长度
	uint8_t* buffer = NULL;//解码数据输出缓存指针
	int i, videoStream;//循环变量，视频流类型标号
	int frameFinished;//解码操作是否成功标识

/*-------------参数初始化------------*/
	//if (argc < 2) {//检查输入参数个数是否正确
	//	fprintf(stderr, "Please provide a movie file\n");
	//	system("pause");
	//	//printf("Please provide a movie file\n");
	//	return -1;
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
			break;//退出循环
		}
	}
	if (videoStream == -1) {//检查文件中是否存在视频流
		return -1; // Didn't find a video stream.
	}

	// Get a pointer to the codec context for the video stream，根据流类型标号从pFormatCtx->streams中取得视频流对应的解码器上下文
	pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;
	/*-----------------------
	 * Find the decoder for the video stream，根据视频流对应的解码器上下文查找对应的解码器，返回对应的解码器(信息结构体)
	 * The stream's information about the codec is in what we call the "codec context.
	 * This contains all the information about the codec that the stream is using
	 -----------------------*/
	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if (pCodec == NULL) {//检查解码器是否匹配
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found.
	}

	// Copy context，复制编解码器上下文对象，用于保存从视频流中抽取的帧
	pCodecCtx = avcodec_alloc_context3(pCodec);//创建AVCodecContext结构体对象pCodecCtx
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {//复制编解码器上下文对象
		fprintf(stderr, "Couldn't copy codec context");
		return -1; // Error copying codec context.
	}

	// Open codec，打开解码器
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		return -1; // Could not open codec.
	}

	// Allocate video frame，为解码后的视频信息结构体分配空间并完成初始化操作(结构体中的图像缓存按照下面两步手动安装)
	pFrame = av_frame_alloc();

	// Allocate an AVFrame structure，为转换PPM文件的结构体分配空间并完成初始化操作
	pFrameRGB = av_frame_alloc();
	if (pFrameRGB == NULL) {//检查初始化操作是否成功
		return -1;
	}

	// Determine required buffer size and allocate buffer，根据像素格式及图像尺寸计算内存大小
	numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
	buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));//为转换后的RGB24图像配置缓存空间

	// Assign appropriate parts of buffer to image planes in pFrameRGB Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
	// 为AVFrame对象安装图像缓存，将out_buffer缓存挂到pFrameYUV->data指针结构上
	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

	// Initialize SWS context for software scaling，设置图像转换像素格式为AV_PIX_FMT_RGB24
	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

	/*--------------循环解码-------------*/
	i = 0;// Read frames(2 packet) and save first five frames to disk,
	/*-----------------------
	 * read in a packet and store it in the AVPacket struct
	 * ffmpeg allocates the internal data for us,which is pointed to by packet.data
	 * this is freed by the av_free_packet()
	 -----------------------*/
	while (av_read_frame(pFormatCtx, &packet) >= 0) {//从视频文件或网络流媒体中依次读取每个图像编码数据包，并存储在AVPacket数据结构中
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
				// Convert the image from its native format to RGB，//将解码后的图像转换为RGB24格式
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

				if (++i <= 5) {// Save the frame to disk，将前5帧图像存储到磁盘上
					SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
				}
			}
		}
		// Free the packet that was allocated by av_read_frame，释放AVPacket数据结构中编码数据指针
		av_packet_unref(&packet);
	}

	/*--------------参数撤销-------------*/
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
