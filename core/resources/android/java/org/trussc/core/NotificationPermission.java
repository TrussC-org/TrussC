package org.trussc.core;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import java.util.WeakHashMap;

/** Notification opt-in; service startup never waits for the permission result. */
public final class NotificationPermission {
    private static final String PERMISSION = "android.permission.POST_NOTIFICATIONS";
    // Accessed only on the UI thread; keys do not retain destroyed Activities.
    private static final WeakHashMap<Activity, Boolean> requested = new WeakHashMap<>();

    public static void request(final Activity activity) {
        if (Build.VERSION.SDK_INT < 33) return;
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (requested.containsKey(activity)
                    || activity.checkSelfPermission(PERMISSION) == PackageManager.PERMISSION_GRANTED) return;
                requested.put(activity, true);
                activity.requestPermissions(new String[] { PERMISSION }, 0);
            }
        });
    }
}
