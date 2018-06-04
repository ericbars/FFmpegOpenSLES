#include <assert.h>
#include <jni.h>
#include <string.h>
#include "com_opensles_ffmpeg_MainActivity.h"
#include "player.h"

// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;

// output mix interfaces
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
static SLEffectSendItf bqPlayerEffectSend;
static SLVolumeItf bqPlayerVolume;
static uint8_t decoded_audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];

// this callback handler is called every time a buffer finishes playing
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
	SLresult result;

	//LOGV2("bqPlayerCallback...");

	if (bq != bqPlayerBufferQueue) {
		LOGV2("bqPlayerCallback : not the same player object.");
		return;
	}

	int decoded_size = audio_decode_frame(decoded_audio_buf,
			sizeof(decoded_audio_buf));
	if (decoded_size > 0) {
		result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue,
				decoded_audio_buf, decoded_size);
		// the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
		// which for this code example would indicate a programming error
		if (SL_RESULT_SUCCESS != result) {
			LOGV2("bqPlayerCallback : bqPlayerBufferQueue Enqueue failure.");
		}
	}
}

int createEngine() {

	SLresult result;

	// create engine
	result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("slCreateEngine failure.");
		return -1;
	}

	// realize the engine
	result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE );
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("engineObject Realize failure.");
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
		return -1;
	}

	// get the engine interface, which is needed in order to create other objects
	result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
			&engineEngine);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("engineObject GetInterface failure.");
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
		return -1;
	}

	// create output mix, with environmental reverb specified as a non-required interface
	const SLInterfaceID ids[1] = { SL_IID_ENVIRONMENTALREVERB };
	const SLboolean req[1] = { SL_BOOLEAN_FALSE };
	result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1,
			ids, req);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("engineObject CreateOutputMix failure.");
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
		return -1;
	}

	// realize the output mix
	result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE );
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("outputMixObject Realize failure.");

		(*outputMixObject)->Destroy(outputMixObject);
		outputMixObject = NULL;
		outputMixEnvironmentalReverb = NULL;
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
		return -1;
	}

	// get the environmental reverb interface
	// this could fail if the environmental reverb effect is not available,
	// either because the feature is not present, excessive CPU load, or
	// the required MODIFY_AUDIO_SETTINGS permission was not requested and granted
	result = (*outputMixObject)->GetInterface(outputMixObject,
			SL_IID_ENVIRONMENTALREVERB, &outputMixEnvironmentalReverb);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("outputMixObject Realize failure.");
		(*outputMixObject)->Destroy(outputMixObject);
		outputMixObject = NULL;
		outputMixEnvironmentalReverb = NULL;
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
		return -1;
	}

	LOGV2("OpenSL ES createEngine success.");
	return 0;
}

int createBufferQueueAudioPlayer() {
	SLresult result;
	SLuint32 channelMask;

	// configure audio source
	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
			SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };

	if (global_context.acodec_ctx->channels == 2)
		channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
	else
		channelMask = SL_SPEAKER_FRONT_CENTER;

	SLDataFormat_PCM format_pcm = { SL_DATAFORMAT_PCM,
			global_context.acodec_ctx->channels,
			global_context.acodec_ctx->sample_rate * 1000,
			SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
			channelMask, SL_BYTEORDER_LITTLEENDIAN };

	SLDataSource audioSrc = { &loc_bufq, &format_pcm };

	// configure audio sink
	SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX,
			outputMixObject };
	SLDataSink audioSnk = { &loc_outmix, NULL };

	// create audio player
	const SLInterfaceID ids[3] = { SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND,
			SL_IID_VOLUME };
	const SLboolean req[3] =
			{ SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
	result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject,
			&audioSrc, &audioSnk, 3, ids, req);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("CreateAudioPlayer failure.");
		return -1;
	}

	// realize the player
	result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE );
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject Realize failure.");
		return -1;
	}

	// get the play interface
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY,
			&bqPlayerPlay);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject GetInterface failure.");
		return -1;
	}

	// get the buffer queue interface
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
			&bqPlayerBufferQueue);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject GetInterface failure.");
		return -1;
	}

	// register callback on the buffer queue
	result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue,
			bqPlayerCallback, NULL);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject RegisterCallback failure.");
		return -1;
	}

	// get the effect send interface
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND,
			&bqPlayerEffectSend);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject GetInterface SL_IID_EFFECTSEND failure.");
		return -1;
	}

	// get the volume interface
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME,
			&bqPlayerVolume);
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject GetInterface SL_IID_VOLUME failure.");
		return -1;
	}

	// set the player's state to playing
	result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING );
	if (SL_RESULT_SUCCESS != result) {
		LOGV2("bqPlayerObject SetPlayState SL_PLAYSTATE_PLAYING failure.");
		return -1;
	}

	LOGV2("OpenSL ES CreateAudioPlayer success.");

	return 0;
}

void fireOnPlayer() {
	bqPlayerCallback(bqPlayerBufferQueue, NULL);
}

/**
 * Destroys the given object instance.
 *
 * @param object object instance. [IN/OUT]
 */
static void DestroyObject(SLObjectItf& object) {
	if (0 != object)
		(*object)->Destroy(object);

	object = 0;
}

void destroyPlayerAndEngine() {
	// Destroy audio player object
	DestroyObject(bqPlayerObject);

	// Destroy output mix object
	DestroyObject(outputMixObject);

	// Destroy the engine instance
	DestroyObject(engineObject);
}

/*
 * Class:     com_opensles_ffmpeg_MainActivity
 * Method:    startAudioPlayer
 * Signature: ()I
 */JNIEXPORT jint JNICALL Java_com_opensles_ffmpeg_MainActivity_startAudioPlayer(
		JNIEnv *, jclass) {
	pthread_t thread;
	pthread_create(&thread, NULL, open_media, NULL);
	return 0;
}

/*
 * Class:     com_opensles_ffmpeg_MainActivity
 * Method:    destroyEngine
 * Signature: ()I
 */JNIEXPORT jint JNICALL Java_com_opensles_ffmpeg_MainActivity_destroyEngine(
		JNIEnv *, jclass) {
	destroyPlayerAndEngine();
	return 0;
}

/*
 * Class:     com_opensles_ffmpeg_MainActivity
 * Method:    stopAudioPlayer
 * Signature: ()I
 */JNIEXPORT jint JNICALL Java_com_opensles_ffmpeg_MainActivity_stopAudioPlayer(
		JNIEnv *, jclass) {
	(*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED );
	global_context.pause = 1;
	global_context.quit = 1;
	usleep(50000);
	return 0;
}
