package org.trussc.core;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

/** Opt-in media playback service. No permission UI and no wake lock. */
public final class ForegroundService extends Service {
    private static final String CHANNEL = "trussc_media_playback";
    private static final int NOTIFICATION = 1594;

    @Override public void onCreate() {
        super.onCreate();
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(
            CHANNEL, "Media playback", NotificationManager.IMPORTANCE_LOW));
        Notification.Builder builder = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(getApplicationInfo().loadLabel(getPackageManager()))
            .setContentText("Background media playback is active")
            .setCategory(Notification.CATEGORY_SERVICE)
            .setOngoing(true);
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            builder.setContentIntent(PendingIntent.getActivity(this, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        }
        startForeground(NOTIFICATION, builder.build());
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        // Explicit opt-in only: don't recreate the service after process death.
        return START_NOT_STICKY;
    }

    @Override public void onDestroy() {
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
