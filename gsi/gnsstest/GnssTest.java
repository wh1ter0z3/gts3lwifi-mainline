// A GPS client for the shell: asks the framework's gps provider for fixes
// every second and reports satellites, NMEA and fixes, but never a position
// (accuracy, altitude yes/no and the satellites used only).
// Build: javac -cp android.jar GnssTest.java; d8 --lib android.jar --min-api 30
// Run:   adb shell CLASSPATH=/data/local/tmp/gnsstest.jar app_process / GnssTest [seconds]
import android.content.AttributionSource;
import android.content.Context;
import android.content.ContextWrapper;
import android.location.GnssStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.location.OnNmeaMessageListener;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.util.Map;
import java.util.TreeMap;

public class GnssTest {
    // The shell's identity (its package has the location permissions)
    static class ShellContext extends ContextWrapper {
        ShellContext(Context base) { super(base); }
        @Override public String getPackageName() { return "com.android.shell"; }
        @Override public String getOpPackageName() { return "com.android.shell"; }
        @Override public String getAttributionTag() { return null; }
        @Override public AttributionSource getAttributionSource() {
            return new AttributionSource.Builder(Process.myUid())
                    .setPackageName("com.android.shell").build();
        }
        @Override public Context getApplicationContext() { return this; }
    }

    static final long t0 = SystemClock.elapsedRealtime();
    static int fixes, nmea, statusReports;
    static final Map<String, Integer> sentences = new TreeMap<>();

    static void log(String s) {
        System.out.printf("%6.1fs %s%n", (SystemClock.elapsedRealtime() - t0) / 1000.0, s);
    }

    public static void main(String[] args) throws Exception {
        final int seconds = args.length > 0 ? Integer.parseInt(args[0]) : 60;
        Looper.prepareMainLooper();
        Class<?> at = Class.forName("android.app.ActivityThread");
        Constructor<?> c = at.getDeclaredConstructor();
        c.setAccessible(true);
        Object thread = c.newInstance();
        Field cur = at.getDeclaredField("sCurrentActivityThread");
        cur.setAccessible(true);
        cur.set(null, thread);
        Context ctx = new ShellContext((Context) at.getDeclaredMethod("getSystemContext").invoke(thread));
        IBinder b = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "location");
        Class<?> ilmClass = Class.forName("android.location.ILocationManager");
        Object ilm = Class.forName("android.location.ILocationManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, b);
        final LocationManager lm = (LocationManager) LocationManager.class
                .getConstructor(Context.class, ilmClass).newInstance(ctx, ilm);
        Handler h = new Handler(Looper.getMainLooper());

        log("gps provider enabled: " + lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                + ", hardware: " + lm.getGnssHardwareModelName());
        final GnssStatus.Callback status = new GnssStatus.Callback() {
            @Override public void onStarted() { log("GNSS started"); }
            @Override public void onStopped() { log("GNSS stopped"); }
            @Override public void onFirstFix(int ms) { log("first fix after " + ms + " ms"); }
            @Override public void onSatelliteStatusChanged(GnssStatus s) {
                if (statusReports++ % 10 != 0) return;
                int used = 0, tracked = 0;
                float best = 0;
                Map<Integer, Integer> perType = new TreeMap<>();
                for (int i = 0; i < s.getSatelliteCount(); i++) {
                    if (s.usedInFix(i)) used++;
                    if (s.getCn0DbHz(i) > 0) tracked++;
                    best = Math.max(best, s.getCn0DbHz(i));
                    perType.merge(s.getConstellationType(i), 1, Integer::sum);
                }
                log(String.format("satellites: %d listed (by type %s), %d with signal, %d used, best C/N0 %.1f",
                        s.getSatelliteCount(), perType, tracked, used, best));
            }
        };
        lm.registerGnssStatusCallback(status, h);
        final OnNmeaMessageListener nmeaListener = (msg, ts) -> {
            nmea++;
            if (msg.length() > 6) sentences.merge(msg.substring(1, 6), 1, Integer::sum);
        };
        lm.addNmeaListener(nmeaListener, h);
        final LocationListener listener = l -> {
            fixes++;
            if (fixes <= 3 || fixes % 10 == 0)
                log(String.format("fix %d: accuracy %.1f m%s%s, age %d ms", fixes, l.getAccuracy(),
                        l.hasAltitude() ? ", altitude" : "", l.hasSpeed() ? ", speed" : "",
                        (SystemClock.elapsedRealtimeNanos() - l.getElapsedRealtimeNanos()) / 1000000));
        };
        lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, listener, Looper.getMainLooper());
        log("requested gps fixes every 1000 ms for " + seconds + " s");
        h.postDelayed(new Runnable() {
            int n;
            @Override public void run() {
                if (++n * 15 >= seconds) {
                    lm.removeUpdates(listener);
                    lm.removeNmeaListener(nmeaListener);
                    lm.unregisterGnssStatusCallback(status);
                    log("done: " + fixes + " fixes, " + nmea + " NMEA sentences " + sentences
                            + ", " + statusReports + " satellite reports");
                    System.exit(0);
                }
                log("so far: " + fixes + " fixes, " + nmea + " NMEA, " + statusReports + " satellite reports");
                h.postDelayed(this, 15000);
            }
        }, 15000);
        Looper.loop();
    }
}
