package com.opensles.ffmpeg;

import android.app.Activity;
import android.os.Bundle;

public class MainActivity extends Activity {
	static {
		System.loadLibrary("ffmpeg");
		System.loadLibrary("audio-jni");
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);
		startAudioPlayer();
	}

	@Override
	protected void onDestroy() {
		stopAudioPlayer();
		destroyEngine();
		super.onDestroy();
	}

	public static native int destroyEngine();

	public static native int startAudioPlayer();

	public static native int stopAudioPlayer();
}
