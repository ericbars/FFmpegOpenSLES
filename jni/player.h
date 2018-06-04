#ifndef __PLAYER_H__
#define __PLAYER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <android/log.h>
#include <time.h>
#include <utime.h>
#include <inttypes.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sched.h>

#include "config.h"

#include "libavutil/log.h"
#include "libavutil/time.h"
#include "libavutil/samplefmt.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include <libavutil/imgutils.h>

#if CONFIG_AVDEVICE
#include "libavdevice/avdevice.h"
#endif
#if CONFIG_AVFILTER
#include "libavfilter/avfilter.h"
#endif

#ifndef UINT64_C
#define UINT64_C uint64_t
#endif

#include <android/log.h>

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	int abort_request;
	int serial;
	pthread_mutex_t *mutex;
} PacketQueue;

typedef struct AudioParams {
	int freq;
	int channels;
	unsigned int channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
} AudioParams;

typedef struct GlobalContexts {
	AVCodecContext *acodec_ctx;
	AVCodecContext *vcodec_ctx;
	AVStream *vstream;
	AVStream *astream;
	AVCodec *vcodec;
	AVCodec *acodec;

	PacketQueue audio_queue;

	int quit;
	int pause;
} GlobalContext;

void packet_queue_init(PacketQueue *q);
int packet_queue_get(PacketQueue *q, AVPacket *pkt);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

int audio_decode_frame(uint8_t *audio_buf, int buf_size);
void* open_media(void *argv);
int createEngine();
int createBufferQueueAudioPlayer();
void fireOnPlayer();

extern GlobalContext global_context;

#define TAG "FFmpeg"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)

#define TAG2 "OpenSLES"
#define LOGV2(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG2, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __PLAYER_H__ */

