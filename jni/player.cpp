#include <stdio.h>
#include <signal.h>

#include "player.h"

#define TEST_FILE_TFCARD "/mnt/extSdCard/clear.ts"
//#define TEST_FILE_TFCARD "/mnt/extSdCard/baidu.mp4"

GlobalContext global_context;

static void sigterm_handler(int sig) {
	av_log(NULL, AV_LOG_ERROR, "sigterm_handler : sig is %d \n", sig);
	exit(123);
}

static void ffmpeg_log_callback(void *ptr, int level, const char *fmt,
		va_list vl) {
	//__android_log_vprint(ANDROID_LOG_DEBUG, "FFmpeg", fmt, vl);
}

void* open_media(void *argv) {
	int i;
	int err = 0;
	int framecnt;
	AVFormatContext *fmt_ctx = NULL;
	AVDictionaryEntry *dict = NULL;
	AVPacket pkt;
	int audio_stream_index = -1;
	bool firstPacket = true;
	pthread_t thread;

	global_context.quit = 0;
	global_context.pause = 0;

	// register INT/TERM signal
	signal(SIGINT, sigterm_handler); /* Interrupt (ANSI).    */
	signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

	av_log_set_callback(ffmpeg_log_callback);

	// set log level
	av_log_set_level(AV_LOG_WARNING);

	/* register all codecs, demux and protocols */
	avfilter_register_all();
	av_register_all();
	avformat_network_init();

	fmt_ctx = avformat_alloc_context();

	err = avformat_open_input(&fmt_ctx, TEST_FILE_TFCARD, NULL, NULL);
	if (err < 0) {
		char errbuf[64];
		av_strerror(err, errbuf, 64);
		av_log(NULL, AV_LOG_ERROR, "avformat_open_input : err is %d , %s\n",
				err, errbuf);
		err = -1;
		goto failure;
	}

	if ((err = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info : err is %d \n",
				err);
		err = -1;
		goto failure;
	}

	// search audio stream in all streams.
	for (i = 0; i < fmt_ctx->nb_streams; i++) {
		// we used the first audio stream
		if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_index = i;
			break;
		}
	}

	// if no audio, exit
	if (-1 == audio_stream_index) {
		goto failure;
	}

	// open audio
	if (-1 != audio_stream_index) {
		global_context.acodec_ctx = fmt_ctx->streams[audio_stream_index]->codec;
		global_context.astream = fmt_ctx->streams[audio_stream_index];
		global_context.acodec = avcodec_find_decoder(
				global_context.acodec_ctx->codec_id);
		//av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1,
		//	&global_context.acodec, 0);
		if (NULL == global_context.acodec) {
			av_log(NULL, AV_LOG_ERROR, "avcodec_find_decoder failure. \n");
			err = -1;
			goto failure;
		}

		//av_opt_set_int(global_context.acodec_ctx, "refcounted_frames", 1, 0);
		if (avcodec_open2(global_context.acodec_ctx, global_context.acodec,
				NULL) < 0) {
			av_log(NULL, AV_LOG_ERROR, "avcodec_open2 failure. \n");
			err = -1;
			goto failure;
		}
	}

	// opensl es init
	createEngine();
	createBufferQueueAudioPlayer();

	// read url media data circle
	while ((av_read_frame(fmt_ctx, &pkt) >= 0) && (!global_context.quit)) {
		if (pkt.stream_index == audio_stream_index) {
			packet_queue_put(&global_context.audio_queue, &pkt);
			if (firstPacket) {
				firstPacket = false;
				fireOnPlayer();
			}
		} else {
			av_free_packet(&pkt);
		}
	}

	// wait exit
	while (!global_context.quit) {
		usleep(1000);
	}

	failure:

	if (fmt_ctx) {
		avformat_close_input(&fmt_ctx);
		avformat_free_context(fmt_ctx);
	}

	avformat_network_deinit();

	return 0;
}

