// vtest: plays a video file in a VideoView (MediaPlayer, as plain players do),
// seeks to 60 s and back to 5 s, and logs the position (tag vtest), then ends.
// adb shell am start -n com.gts3.vtest/.Main [-e path <file>]
package com.gts3.vtest;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.VideoView;

public class Main extends Activity {
	private VideoView v;
	private final Handler h = new Handler(Looper.getMainLooper());

	@Override
	protected void onCreate(Bundle b) {
		super.onCreate(b);
		v = new VideoView(this);
		setContentView(v);
		String path = getIntent().getStringExtra("path");
		v.setVideoPath(path != null ? path : "/data/local/tmp/h264-1080p-long.mp4");
		v.setOnPreparedListener(mp -> {
			Log.i("vtest", "prepared " + mp.getVideoWidth() + "x" + mp.getVideoHeight());
			v.start();
		});
		v.setOnErrorListener((mp, what, extra) -> {
			Log.e("vtest", "error " + what + " " + extra);
			return true;
		});
		at(9000, () -> log("before seek"));
		at(10000, () -> { v.seekTo(60000); log("seek to 60 s"); });
		at(15000, () -> log("after seek"));
		at(16000, () -> { v.seekTo(5000); log("seek to 5 s"); });
		at(21000, () -> log("after seek"));
		at(22000, () -> { log("done"); finish(); });
	}

	private void at(long ms, Runnable r) { h.postDelayed(r, ms); }

	private void log(String s) {
		Log.i("vtest", s + ": position " + v.getCurrentPosition() + " ms, playing " + v.isPlaying());
	}
}
