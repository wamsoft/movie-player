package jp.wamsoft.testmovieplayer;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.annotation.NonNull;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.FileDescriptor;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.Random;

/**
 * movieplayerのテストプログラム ムービーサイズでBitmapを作成して、SurfaceViewのCanvasに描画する
 * 描画は SurfaceView 全体にアスペクト維持で letterbox フィットする。
 */

public class TestMovieView extends SurfaceView implements SurfaceHolder.Callback {
    Random random = new Random();
    Thread thread = null; // 表示用スレッド
    Bitmap movieBitmap; // ムービー用ビットマップ

    int width = 0;
    int height = 0;

    // ファイルはリソースから取得する
    private static Resources mResources;

    public TestMovieView(Context context) {
        super(context);
        init(context);
    }

    public TestMovieView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    public TestMovieView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context);
    }

    public TestMovieView(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        init(context);
    }

    private void init(Context context) {
        getHolder().addCallback(this);
        mResources = context.getResources();
    }

    // 画像を表示するスレッド
    private class DrawThread extends Thread {
        public void run() {
            SurfaceHolder holder = getHolder();
            while (mPlayer != null && mPlayer.isPlaying()) {
                if (updateMovie()) {
                    Canvas canvas = holder.lockCanvas();
                    if (canvas != null) {
                        drawMovie(canvas);
                        holder.unlockCanvasAndPost(canvas);
                    }
                }
                try {
                    sleep(10);
                } catch (InterruptedException e) {
                }
            }
            Log.d("TestMovieView", "******* isPlaying() done ******");
        }
    }

    // ムービーセットアップ
    private void setupMovie() {

        if (mPlayer == null) {
            mPlayer = new MyMoviePlayer();
        }
        //mPlayer.createPlayer("Big_Buck_Bunny_1080_10s_1MB_VP8.webm");
        mPlayer.createPlayer("title.webm");

        width = mPlayer.getWidth();
        height = mPlayer.getHeight();

        // ピクセルのパック表現@LE: 0xAABBGGRR
        // (A & 0xff) << 24 | (B & 0xff) << 16 | (G & 0xff) << 8 | (R & 0xff);
        movieBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);

        // 開始
        boolean loop = true;
        mPlayer.startPlayer(loop);
    }

    // レンダリングされた動画フレームを取得してBitmapにコピー
    private boolean updateMovie() {
        if (mPlayer != null) {
            ByteBuffer buffer = mPlayer.getUpdatedBuffer();
            if (buffer != null) {
                movieBitmap.copyPixelsFromBuffer(buffer);
                return true;
            }
        }
        return false;
    }

    // 画像を表示する (SurfaceView 全体にアスペクト維持で letterbox フィット)
    private void drawMovie(Canvas canvas) {
        canvas.drawColor(Color.BLACK);
        if (movieBitmap == null || width <= 0 || height <= 0) {
            return;
        }
        int cw = canvas.getWidth();
        int ch = canvas.getHeight();
        if (cw <= 0 || ch <= 0) {
            return;
        }
        // 縦横どちらか余りが出る側に合わせて scale
        float scale = Math.min((float) cw / width, (float) ch / height);
        int dw = Math.max(1, Math.round(width * scale));
        int dh = Math.max(1, Math.round(height * scale));
        int dx = (cw - dw) / 2;
        int dy = (ch - dh) / 2;
        Rect src = new Rect(0, 0, width, height);
        Rect dst = new Rect(dx, dy, dx + dw, dy + dh);
        Paint paint = new Paint();
        paint.setFilterBitmap(true);
        canvas.drawBitmap(movieBitmap, src, dst, paint);
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        // ムービーをセットアップ
        setupMovie();
        // 描画スレッドを開始
        thread = new DrawThread();
        thread.start();
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {}

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        thread = null;
        if (mPlayer != null) {
            mPlayer.stopPlayer();
             // Playスレッドが止まるのを待つべきだが、とりあえずstop
        }
    }

    private MyMoviePlayer mPlayer;

    public int duration() {
        return mPlayer != null ? mPlayer.getDuration() : 0;
    }

    public int position() {
        return mPlayer != null ? mPlayer.getPosition() : 0;
    }
}
