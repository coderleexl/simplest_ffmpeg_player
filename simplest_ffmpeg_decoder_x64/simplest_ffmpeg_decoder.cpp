// /**
//  * 最简单的基于FFmpeg的视频解码器
//  * Simplest FFmpeg Decoder
//  *
//  * 雷霄骅 Lei Xiaohua
//  * leixiaohua1020@126.com
//  * 中国传媒大学/数字电视技术
//  * Communication University of China / Digital TV Technology
//  * http://blog.csdn.net/leixiaohua1020
//  *
//  *
//  * 本程序实现了视频文件解码为YUV数据。它使用了libavcodec和
//  * libavformat。是最简单的FFmpeg视频解码方面的教程。
//  * 通过学习本例子可以了解FFmpeg的解码流程。
//  * This software is a simplest decoder based on FFmpeg.
//  * It decodes video to YUV pixel data.
//  * It uses libavcodec and libavformat.
//  * Suitable for beginner of FFmpeg.
//  *
//  */
// 
// 
// 
#include <stdio.h>
#include <thread>
#include <chrono>
#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
 //Windows
extern "C"
{
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include "libswscale/swscale.h"
};
#include <iostream>
#else
 //Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
};
#endif
#endif



static AVBufferRef* hw_device_ctx = NULL;
static enum AVPixelFormat hw_pix_fmt;
static FILE* output_file = NULL;
static int hw_decoder_init(AVCodecContext* ctx, const enum AVHWDeviceType type)
{
	int err = 0;
	//创建硬件设备信息上下文 
	if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
		NULL, NULL, 0)) < 0) {
		fprintf(stderr, "Failed to create specified HW device.\n");
		return err;
	}
	//绑定编解码器上下文和硬件设备信息上下文
	ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
	return err;
}
static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
	const enum AVPixelFormat* pix_fmts)
{
	const enum AVPixelFormat* p;
	for (p = pix_fmts; *p != -1; p++) {
		if (*p == hw_pix_fmt)
			return *p;
	}
	fprintf(stderr, "Failed to get HW surface format.\n");
	return AV_PIX_FMT_NONE;
}
static int decode_write(AVCodecContext* avctx, AVPacket* packet)
{
	AVFrame* frame = NULL, * sw_frame = NULL;
	AVFrame* tmp_frame = NULL;
	uint8_t* buffer = NULL;
	int size;
	int ret = 0;
	ret = avcodec_send_packet(avctx, packet);
	if (ret < 0) {
		fprintf(stderr, "Error during decoding\n");
		return ret;
	}
	while (1) {
		if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
			fprintf(stderr, "Can not alloc frame\n");
			ret = AVERROR(ENOMEM);
			goto fail;
		}
		ret = avcodec_receive_frame(avctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			av_frame_free(&frame);
			av_frame_free(&sw_frame);
			return 0;
		}
		else if (ret < 0) {
			fprintf(stderr, "Error while decoding\n");
			goto fail;
		}
		if (frame->format == hw_pix_fmt) {
			/* retrieve data from GPU to CPU */
			if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
				fprintf(stderr, "Error transferring the data to system memory\n");
				goto fail;
			}
			tmp_frame = sw_frame;
		}
		else
			tmp_frame = frame;
		size = av_image_get_buffer_size((AVPixelFormat)tmp_frame->format, tmp_frame->width,
			tmp_frame->height, 1);
		buffer = (uint8_t*)av_malloc(size);
		if (!buffer) {
			fprintf(stderr, "Can not alloc buffer\n");
			ret = AVERROR(ENOMEM);
			goto fail;
		}
		ret = av_image_copy_to_buffer(buffer, size,
			(const uint8_t* const*)tmp_frame->data,
			(const int*)tmp_frame->linesize, (AVPixelFormat)tmp_frame->format,
			tmp_frame->width, tmp_frame->height, 1);
		if (ret < 0) {
			fprintf(stderr, "Can not copy image to buffer\n");
			goto fail;
		}
		if ((ret = fwrite(buffer, 1, size, output_file)) < 0) {
			fprintf(stderr, "Failed to dump raw data.\n");
			goto fail;
		}
	fail:
		av_frame_free(&frame);
		av_frame_free(&sw_frame);
		av_freep(&buffer);
		if (ret < 0)
			return ret;
	}
}
int ffmpegGpuDecode(int argc, char* argv[])
{
	AVFormatContext* input_ctx = NULL;
	int video_stream, ret;
	AVStream* video = NULL;
	AVCodecContext* decoder_ctx = NULL;
	AVCodec* decoder = NULL;
	AVPacket packet;
	enum AVHWDeviceType type;
	int i;
	if (argc < 4) {
		fprintf(stderr, "Usage: %s <device type> <input file> <output file>\n", argv[0]);
		return -1;
	}
	//通过你传入的名字来找到对应的硬件解码类型
	type = av_hwdevice_find_type_by_name(argv[1]);
	if (type == AV_HWDEVICE_TYPE_NONE) {
		fprintf(stderr, "Device type %s is not supported.\n", argv[1]);
		fprintf(stderr, "Available device types:");
		while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
			fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
		fprintf(stderr, "\n");
		return -1;
	}
	/* open the input file */
	if (avformat_open_input(&input_ctx, argv[2], NULL, NULL) != 0) {
		fprintf(stderr, "Cannot open input file '%s'\n", argv[2]);
		return -1;
	}
	if (avformat_find_stream_info(input_ctx, NULL) < 0) {
		fprintf(stderr, "Cannot find input stream information.\n");
		return -1;
	}
	/* find the video stream information */
	ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, (const AVCodec**)(&decoder), 0);
	if (ret < 0) {
		fprintf(stderr, "Cannot find a video stream in the input file\n");
		return -1;
	}
	video_stream = ret;
	//去遍历所有编解码器支持的硬件解码配置 如果和之前你指定的是一样的 那么就可以继续执行了 不然就找不到
	for (i = 0;; i++) {
		const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
		if (!config) {
			fprintf(stderr, "Decoder %s does not support device type %s.\n",
				decoder->name, av_hwdevice_get_type_name(type));
			return -1;
		}
		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
			config->device_type == type) {
			//把硬件支持的像素格式设置进去
			hw_pix_fmt = config->pix_fmt;
			break;
		}
	}
	if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
		return AVERROR(ENOMEM);
	video = input_ctx->streams[video_stream];
	if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
		return -1;
	//填入回调函数 通过这个函数 编解码器能够知道显卡支持的像素格式
	decoder_ctx->get_format = get_hw_format;
	if (hw_decoder_init(decoder_ctx, type) < 0)
		return -1;
	//绑定完成后 打开编解码器
	if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
		fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
		return -1;
	}
	/* open the file to dump raw data */
	output_file = fopen(argv[3], "w+");
	/* actual decoding and dump the raw data */
	while (ret >= 0) {
		if ((ret = av_read_frame(input_ctx, &packet)) < 0)
			break;
		if (video_stream == packet.stream_index)
			ret = decode_write(decoder_ctx, &packet);
		av_packet_unref(&packet);
	}
	/* flush the decoder */
	packet.data = NULL;
	packet.size = 0;
	ret = decode_write(decoder_ctx, &packet);
	av_packet_unref(&packet);
	if (output_file)
		fclose(output_file);
	avcodec_free_context(&decoder_ctx);
	avformat_close_input(&input_ctx);
	av_buffer_unref(&hw_device_ctx);
	return 0;
}

int IterAllCodec()
{
	// 获取支持的编码器
	void* opaque = nullptr;
	const AVCodec* codec = av_codec_iterate(&opaque);
	while (codec != nullptr) {
		if (codec->type == AVMEDIA_TYPE_VIDEO) {
			std::string name(codec->name);
			if (codec->id == AV_CODEC_ID_H264)
			{
				std::cout << "编码器名称：" << codec->name << std::endl;
				std::cout << "编码器描述：" << codec->long_name << std::endl;
				std::cout << "编码器ID：" << codec->id << std::endl;
				std::cout << std::endl;
			}
			// 			std::cout << "编码器名称：" << codec->name << std::endl;
			// 			std::cout << "编码器描述：" << codec->long_name << std::endl;
			// 			std::cout << std::endl;
		}
		codec = av_codec_iterate(&opaque);
	}

	return 0;
}

int DecodeByCpu(int argc, char* argv[]) {
	AVFormatContext* pFormatCtx;
	int				i, videoindex;
	AVCodecContext* pCodecCtx;
	AVCodec* pCodec;
	AVFrame* pFrame, * pFrameYUV;
	unsigned char* out_buffer;
	AVPacket* packet;
	int y_size;
	int ret, got_picture;
	struct SwsContext* img_convert_ctx;

	char filepath[] = "Titanic.mkv";

	FILE* fp_yuv = fopen("output.yuv", "wb+");

	avformat_network_init();
	pFormatCtx = avformat_alloc_context();

	//打开流
	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}

	//找到视频信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}

	//寻找视频索引
	videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (videoindex < 0) {
		fprintf(stderr, "找不到视频流\n");
		return -1;
	}

	//寻找视频编码器
	pCodec = (AVCodec*)avcodec_find_decoder(pFormatCtx->streams[videoindex]->codecpar->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}


	//创建解码器上下文
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar) < 0) {
		fprintf(stderr, "无法初始化解码器上下文\n");
		return -1;
	}

	// 获取支持的解码器

// 	pCodecCtx->hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_QSV);
// 	av_hwdevice_ctx_init(pCodecCtx->hw_device_ctx);
// 	AVHWDeviceContext* hwDeviceContext = (AVHWDeviceContext*)pCodecCtx->hw_device_ctx->data;
// 	hwDeviceContext->hwctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_QSV);


	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}

	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);



	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	//Output Info-----------------------------
	printf("--------------- File Information ----------------\n");
	av_dump_format(pFormatCtx, 0, filepath, 0);
	printf("-------------------------------------------------\n");
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	while (av_read_frame(pFormatCtx, packet) == 0) {
		if (packet->stream_index == videoindex) {
			if (avcodec_send_packet(pCodecCtx, packet) == 0) {
				while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
					// 这里可以进行处理，例如将解码后的帧保存到文件
					sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
						pFrameYUV->data, pFrameYUV->linesize);
					y_size = pCodecCtx->width * pCodecCtx->height;
					fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
					fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
					fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
					printf("Succeed to decode 1 frame!\n");
				}
			}
		}
		av_packet_unref(packet);
	}


	sws_freeContext(img_convert_ctx);
	fclose(fp_yuv);

	av_packet_free(&packet);
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;
}

int DecodeByGpu(int argc, char* argv[]) {
	AVFormatContext* pFormatCtx;
	int				i, videoindex;
	AVCodecContext* pCodecCtx;
	AVCodec* pCodec;
	AVFrame* pFrame, * pFrameYUV;
	unsigned char* out_buffer;
	AVPacket* packet;
	int y_size;
	int ret, got_picture;
	struct SwsContext* img_convert_ctx;

	char filepath[] = "Titanic.mkv";

	FILE* fp_yuv = fopen("output.yuv", "wb+");

	avformat_network_init();
	pFormatCtx = avformat_alloc_context();

	//打开流
	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}

	//找到视频信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}

	//寻找视频索引
	videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (videoindex < 0) {
		fprintf(stderr, "找不到视频流\n");
		return -1;
	}

	if (pFormatCtx->streams[videoindex]->codecpar->codec_id == AV_CODEC_ID_H264)
	{
		//寻找视频编码器
		pCodec = (AVCodec*)avcodec_find_decoder_by_name("h264_cuvid");
		if (pCodec == NULL) {
			printf("Codec not found.\n");
			return -1;
		}

		//去遍历所有编解码器支持的硬件解码配置 如果和之前你指定的是一样的 那么就可以继续执行了 不然就找不到
		for (i = 0;; i++) {
			const AVCodecHWConfig* config = avcodec_get_hw_config(pCodec, i);
			if (!config) {
				fprintf(stderr, "Decoder %s does not support device type %s.\n",
					pCodec->name, AV_HWDEVICE_TYPE_D3D11VA);
				return -1;
			}
			if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
				config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
				//把硬件支持的像素格式设置进去
				hw_pix_fmt = config->pix_fmt;
				break;
			}
		}
	}

	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	//创建解码器上下文
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar) < 0) {
		fprintf(stderr, "无法初始化解码器上下文\n");
		return -1;
	}

	pCodecCtx->get_format = get_hw_format;

	if (hw_decoder_init(pCodecCtx, AV_HWDEVICE_TYPE_QSV) < 0)
		return -1;

	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}

	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);



	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	//Output Info-----------------------------
	printf("--------------- File Information ----------------\n");
	av_dump_format(pFormatCtx, 0, filepath, 0);
	printf("-------------------------------------------------\n");
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	while (av_read_frame(pFormatCtx, packet) == 0) {
		if (packet->stream_index == videoindex) {
			if (avcodec_send_packet(pCodecCtx, packet) == 0) {
				while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
					// 这里可以进行处理，例如将解码后的帧保存到文件
					sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
						pFrameYUV->data, pFrameYUV->linesize);
					y_size = pCodecCtx->width * pCodecCtx->height;
					fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
					fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
					fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
					printf("Succeed to decode 1 frame!\n");
				}
			}
		}
		av_packet_unref(packet);
	}


	sws_freeContext(img_convert_ctx);
	fclose(fp_yuv);

	av_packet_free(&packet);
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;
}

int main(int argc, char* argv[])
{
	char* argvc[] = { "4", "dxva2","Titanic.mkv","output.mp4" };
	char* hwList[] = { "cuda", "vaapi", "dxva2", "qsv", "d3d11va", "opencl", "vulkan" };

	for (int i = 0; i < 6; ++i)
	{
		argvc[1] = hwList[i];
		if (ffmpegGpuDecode(4, argvc) < 0) {
			std::cout << "ffmpeg gpu decode error: hwname: " << hwList[i] << std::endl;
		}
		else {
			break;
		}
	}

	// 	IterAllCodec();
	// 	while (true)
	// 	{
	// 		DecodeByGpu(argc, argv);
	// 	}
}


// extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavutil/opt.h>
// #include "libavformat/avformat.h"
// #include "libswscale/swscale.h"
// #include "libavutil/imgutils.h"
// }
// 
// int main() {
// 
// 	// 打开输入文件
// 	const char* input_filename = "Titanic.mkv";
// 	AVFormatContext* input_format_ctx = avformat_alloc_context();
// 	if (avformat_open_input(&input_format_ctx, input_filename, nullptr, nullptr) != 0) {
// 		fprintf(stderr, "无法打开输入文件\n");
// 		return -1;
// 	}
// 
// 	// 获取流信息
// 	if (avformat_find_stream_info(input_format_ctx, nullptr) < 0) {
// 		fprintf(stderr, "无法获取流信息\n");
// 		return -1;
// 	}
// 
// 	// 找到视频流索引
// 	int video_stream_index = av_find_best_stream(input_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
// 	if (video_stream_index < 0) {
// 		fprintf(stderr, "找不到视频流\n");
// 		return -1;
// 	}
// 
// 	// 获取视频解码器
// 	AVCodec* codec = (AVCodec*)avcodec_find_decoder(input_format_ctx->streams[video_stream_index]->codecpar->codec_id);
// 	if (!codec) {
// 		fprintf(stderr, "找不到解码器\n");
// 		return -1;
// 	}
// 
// 	// 创建解码器上下文
// 	AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
// 	if (avcodec_parameters_to_context(codec_ctx, input_format_ctx->streams[video_stream_index]->codecpar) < 0) {
// 		fprintf(stderr, "无法初始化解码器上下文\n");
// 		return -1;
// 	}
// 
// 	// 打开解码器
// 	if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
// 		fprintf(stderr, "无法打开解码器\n");
// 		return -1;
// 	}
// 
// 	// 打开输出文件
// 	const char* output_filename = "output.yuv";
// 	FILE* output_file = fopen(output_filename, "wb");
// 	if (!output_file) {
// 		fprintf(stderr, "无法打开输出文件\n");
// 		return -1;
// 	}
// 
// 	// 初始化帧和数据包
// 	AVFrame* frame = av_frame_alloc();
// 	AVPacket packet;
// 
// 	// 读取帧并进行解码
// 	while (av_read_frame(input_format_ctx, &packet) == 0) {
// 		if (packet.stream_index == video_stream_index) {
// 			if (avcodec_send_packet(codec_ctx, &packet) == 0) {
// 				while (avcodec_receive_frame(codec_ctx, frame) == 0) {
// 					// 这里可以进行处理，例如将解码后的帧保存到文件
// 					fwrite(frame->data[0], 1, frame->linesize[0] * frame->height, output_file);
// 					fwrite(frame->data[1], 1, frame->linesize[1] * frame->height / 2, output_file);
// 					fwrite(frame->data[2], 1, frame->linesize[2] * frame->height / 2, output_file);
// 				}
// 			}
// 		}
// 		av_packet_unref(&packet);
// 	}
// 
// 	// 释放资源
// 	av_frame_free(&frame);
// 	avcodec_free_context(&codec_ctx);
// 	avformat_close_input(&input_format_ctx);
// 	fclose(output_file);
// 
// 	return 0;
// }
