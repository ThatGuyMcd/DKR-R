package io.github.thatguymcd.dkrr;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.widget.Toast;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Opt-in export of an explicit log allowlist, never ROMs, saves or profiles. */
final class AndroidDiagnostics {
    private static final int EXPORT_DIAGNOSTICS = 702;
    private static final int MAX_LOG_BYTES = 1024 * 1024;
    private final Activity activity;
    private boolean busy; // UI thread only.
    private File snapshot;

    AndroidDiagnostics(Activity activity) { this.activity = activity; }
    boolean isBusy() { return busy; }

    void start(ExecutorService io) {
        if (busy) return;
        busy = true;
        toast("Preparing startup diagnostics...");
        io.execute(() -> {
            File report = null;
            try {
                report = File.createTempFile("dkr-startup-", ".zip", activity.getCacheDir());
                android.content.pm.PackageInfo app = activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0);
                try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(report))) {
                    text(zip, "device.txt", "DKR-R Android startup diagnostics\n"
                        + "App: " + app.versionName + " (code " + app.getLongVersionCode() + ")"
                        + "\nNative library SHA-256: " + nativeLibraryHash()
                        + "\nDevice: " + Build.MANUFACTURER + " " + Build.MODEL
                        + "\nAndroid: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")"
                        + "\nBuild: " + Build.DISPLAY
                        + "\nNo ROMs, saves, friend profiles or settings files are included."
                        + "\nLogs may contain local paths or session information. Review before sharing.\n");
                    log(zip, "runtime.log", "config/logs/runtime.log");
                    log(zip, "runtime-previous.log", "config/logs/runtime-previous.log");
                    log(zip, "rt64.log", "config/rt64/rt64.log");
                    log(zip, "performance.txt", "performance-report.txt");
                    if (Build.VERSION.SDK_INT >= 30) addExitInfo(zip);
                }
                final File ready = report;
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) {
                        ready.delete(); busy = false; return;
                    }
                    snapshot = ready;
                    Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("application/zip");
                    intent.putExtra(Intent.EXTRA_TITLE, "DKR-R-Android-startup.zip");
                    try { activity.startActivityForResult(intent, EXPORT_DIAGNOSTICS); }
                    catch (RuntimeException error) { finish("Cannot open Android's export picker"); }
                });
            } catch (Exception error) {
                if (report != null) report.delete();
                activity.runOnUiThread(() -> finish("Diagnostic export failed: " + error.getMessage()));
            }
        });
    }

    boolean onResult(int requestCode, int resultCode, Intent data, ExecutorService io) {
        if (requestCode != EXPORT_DIAGNOSTICS) return false;
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null || snapshot == null) {
            finish(null); return true;
        }
        final File source = snapshot;
        final Uri destination = data.getData();
        io.execute(() -> {
            String message;
            try (InputStream input = new FileInputStream(source);
                 OutputStream output = activity.getContentResolver().openOutputStream(destination, "wt")) {
                if (output == null) throw new IOException("Cannot write chosen document");
                copy(input, output, Long.MAX_VALUE);
                message = "Startup diagnostics exported";
            } catch (Exception error) { message = "Diagnostic export failed: " + error.getMessage(); }
            final String result = message;
            activity.runOnUiThread(() -> finish(result));
        });
        return true;
    }

    private void finish(String message) {
        if (snapshot != null) snapshot.delete();
        snapshot = null; busy = false;
        if (message != null) toast(message);
    }
    private String nativeLibraryHash() {
        // Runs on the export worker, never at startup/on the UI thread.
        try (InputStream input = new FileInputStream(new File(activity.getApplicationInfo().nativeLibraryDir, "libDKR-R.so"))) {
            java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[64 * 1024];
            for (int count; (count = input.read(buffer)) != -1;) digest.update(buffer, 0, count);
            StringBuilder text = new StringBuilder(64);
            for (byte value : digest.digest()) text.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
            return text.toString();
        } catch (Exception unavailable) { return "unavailable"; }
    }
    private void toast(String message) { Toast.makeText(activity, message, Toast.LENGTH_LONG).show(); }

    private void log(ZipOutputStream zip, String entry, String relative) throws IOException {
        DiagnosticLogSnapshot.write(zip, entry, new File(activity.getFilesDir(), relative), MAX_LOG_BYTES);
    }

    @android.annotation.TargetApi(30)
    private void addExitInfo(ZipOutputStream zip) throws IOException {
        ActivityManager manager = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
        if (manager == null) return;
        try {
            int index = 0;
            for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(activity.getPackageName(), 0, 3)) {
                String prefix = "previous-exit-" + index++;
                text(zip, prefix + ".txt", "Timestamp: " + exit.getTimestamp()
                    + "\nReason: " + exit.getReason() + "\nStatus: " + exit.getStatus()
                    + "\nDescription: " + exit.getDescription() + "\n");
                // Android 12+ supplies the app's native tombstone protobuf here.
                // Do not collect other applications' logs or use broad logcat access.
                byte[] traceBytes = null;
                String traceError = null;
                try (InputStream trace = exit.getTraceInputStream()) {
                    if (trace != null) {
                        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                        copy(trace, bytes, 4L * MAX_LOG_BYTES);
                        traceBytes = bytes.toByteArray();
                    }
                } catch (IOException | SecurityException error) {
                    traceError = "Android's previous native trace is unavailable.\n";
                }
                if (traceBytes != null) {
                    zip.putNextEntry(new ZipEntry(prefix + ".trace"));
                    zip.write(traceBytes); zip.closeEntry();
                }
                if (traceError != null) text(zip, prefix + "-trace-unavailable.txt", traceError);
            }
        } catch (SecurityException error) {
            text(zip, "exit-info-unavailable.txt", "Android did not grant access to previous exit details.\n");
        }
    }

    private static void text(ZipOutputStream zip, String entry, String value) throws IOException {
        zip.putNextEntry(new ZipEntry(entry));
        zip.write(value.getBytes(StandardCharsets.UTF_8)); zip.closeEntry();
    }
    private static void copy(InputStream input, OutputStream output, long limit) throws IOException {
        byte[] buffer = new byte[16 * 1024];
        for (int count; limit > 0 && (count = input.read(buffer, 0, (int)Math.min(buffer.length, limit))) != -1;) {
            output.write(buffer, 0, count); limit -= count;
        }
    }
}
