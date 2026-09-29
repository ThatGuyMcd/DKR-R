package io.github.thatguymcd.dkrr;

import android.app.AlertDialog;
import android.os.Build;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;

/** Bounded opt-in capture independent of the game renderer or SDL event loop. */
final class PerformanceRecorder {
    private final DkrActivity activity;
    private final ExecutorService io;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final StringBuilder samples = new StringBuilder(8192);
    private boolean running, finishing, destroyed;
    private long started, cpuStarted, startedUtc;
    private int sampleCount;
    PerformanceRecorder(DkrActivity activity, ExecutorService io) { this.activity = activity; this.io = io; }

    void show() {
        if (destroyed || activity.isFinishing() || finishing) return;
        AlertDialog.Builder dialog = new AlertDialog.Builder(activity).setTitle("Performance report")
            .setMessage(running ? "Recording up to 60 seconds. Continue playing, or stop and export now."
                : "Records frame intervals, renderer work, memory and thermal status for 60 seconds. No video, ROMs or saves are recorded. Start here, then close the overlay and play the slow scene. Saved settings are not changed.")
            .setNegativeButton("Return to game", null);
        if (running) dialog.setPositiveButton("Stop and export", (d,w) -> stop(true));
        else dialog.setPositiveButton("Start 60 seconds", (d,w) -> start())
            .setNeutralButton("Export latest report", (d,w) -> activity.exportDiagnostics());
        dialog.show();
    }
    private void start() {
        if (running || finishing || destroyed) return;
        samples.setLength(0); sampleCount = 0;
        samples.append("Android performance samples: wall-ms, process-CPU-ms, native-heap-bytes, PSS-KiB, thermal-status\n");
        started = SystemClock.elapsedRealtime(); cpuStarted = android.os.Process.getElapsedCpuTime();
        startedUtc = System.currentTimeMillis();
        DkrActivity.beginPerformanceCapture(); running = true;
        handler.post(tick);
        Toast.makeText(activity, "Recording for 60 seconds. Return to the game.", Toast.LENGTH_LONG).show();
    }
    private final Runnable tick = new Runnable() { public void run() {
        if (!running || destroyed) return;
        if (++sampleCount > 61 || SystemClock.elapsedRealtime() - started >= 60000) { stop(false); return; }
        int thermal = -1;
        if (Build.VERSION.SDK_INT >= 29) {
            PowerManager pm = (PowerManager) activity.getSystemService(android.content.Context.POWER_SERVICE);
            if (pm != null) thermal = pm.getCurrentThermalStatus();
        }
        samples.append(SystemClock.elapsedRealtime() - started).append(',')
            .append(android.os.Process.getElapsedCpuTime() - cpuStarted).append(',')
            .append(Debug.getNativeHeapAllocatedSize()).append(',').append(Debug.getPss()).append(',')
            .append(thermal).append('\n');
        handler.postDelayed(this, 1000);
    }};
    private void stop(boolean export) {
        if (!running) return;
        running = false; finishing = true; handler.removeCallbacks(tick);
        final String platform = samples.toString();
        final long captureStartedUtc = startedUtc, captureEndedUtc = System.currentTimeMillis();
        final long captureStartedUptime = started;
        io.execute(() -> {
            String error = null;
            try {
                String report = activity.getBuildIdentity() + "\nCapture UTC milliseconds: "
                    + captureStartedUtc + ".." + captureEndedUtc + "\nCapture start uptime ms: "
                    + captureStartedUptime + "\nThis is the latest completed capture, not necessarily the latest game session.\n"
                    + DkrActivity.endPerformanceCapture() + platform;
                android.util.AtomicFile destination = new android.util.AtomicFile(new File(activity.getFilesDir(), "performance-report.txt"));
                FileOutputStream output = destination.startWrite();
                try { output.write(report.getBytes(StandardCharsets.UTF_8)); destination.finishWrite(output); }
                catch (Exception failure) { destination.failWrite(output); throw failure; }
            } catch (Exception failure) { error = "Could not save performance report: " + failure.getMessage(); }
            final String result = error;
            handler.post(() -> {
                finishing = false;
                if (destroyed) return;
                Toast.makeText(activity, result != null ? result : "Performance report saved. Export from About → Support.", Toast.LENGTH_LONG).show();
                if (export && result == null) activity.exportDiagnostics();
            });
        });
    }
    void destroy() { if (running) stop(false); destroyed = true; handler.removeCallbacks(tick); }
}
