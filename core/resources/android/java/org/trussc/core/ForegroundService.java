package org.trussc.core;

import android.app.Activity;
import android.app.Application;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;

/** Opt-in media playback service. No permission UI and no wake lock. */
public final class ForegroundService extends Service implements Application.ActivityLifecycleCallbacks {
    private static final String CHANNEL = "trussc_media_playback";
    private static final int NOTIFICATION = 1594;
    // Service and Activity lifecycle callbacks run on the main thread.
    private Notification notification;
    private boolean notificationPostedWhileGranted;

    private boolean notificationPermissionGranted() {
        return Build.VERSION.SDK_INT < 33
            || checkSelfPermission("android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED;
    }

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
            // Reuse the NativeActivity; CLEAR_TOP alone recreates a standard activity.
            launch.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            builder.setContentIntent(PendingIntent.getActivity(this, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        }
        notification = builder.build();
        notificationPostedWhileGranted = notificationPermissionGranted();
        startForeground(NOTIFICATION, notification);
        getApplication().registerActivityLifecycleCallbacks(this);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        // Explicit opt-in only: don't recreate the service after process death.
        return START_NOT_STICKY;
    }

    @Override public void onDestroy() {
        getApplication().unregisterActivityLifecycleCallbacks(this);
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    @Override public void onActivityResumed(Activity activity) {
        if (!notificationPostedWhileGranted && notificationPermissionGranted()) {
            getSystemService(NotificationManager.class).notify(NOTIFICATION, notification);
            notificationPostedWhileGranted = true;
        }
    }

    @Override public void onActivityCreated(Activity activity, Bundle state) {}
    @Override public void onActivityStarted(Activity activity) {}
    @Override public void onActivityPaused(Activity activity) {}
    @Override public void onActivityStopped(Activity activity) {}
    @Override public void onActivitySaveInstanceState(Activity activity, Bundle state) {}
    @Override public void onActivityDestroyed(Activity activity) {}

    @Override public IBinder onBind(Intent intent) { return null; }
}
