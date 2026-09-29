package io.github.thatguymcd.dkrr;

import org.libsdl.app.SDLActivity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.widget.Toast;
import java.io.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Android-owned files only: users grant access to individual imports via SAF. */
public final class DkrActivity extends SDLActivity {
    // Preserve SDL's callbacks in full. Publish the valid holder before SDL
    // wakes its native thread, and unpublish before destruction. No Vulkan work
    // runs on Android's UI thread.
    @Override protected org.libsdl.app.SDLSurface createSDLSurface(android.content.Context context) {
        return new org.libsdl.app.SDLSurface(context) {
            @Override public void surfaceCreated(android.view.SurfaceHolder holder) {
                publishSurface(holder.getSurface());
                super.surfaceCreated(holder);
            }
            @Override public void surfaceChanged(android.view.SurfaceHolder holder, int format, int width, int height) {
                publishSurface(holder.getSurface());
                super.surfaceChanged(holder, format, width, height);
            }
            @Override public void surfaceDestroyed(android.view.SurfaceHolder holder) {
                publishSurface(null);
                super.surfaceDestroyed(holder);
            }
        };
    }
    private static final int PICK_DOCUMENT = 701;
    private static final long MAX_IMPORT_BYTES = 1024L * 1024 * 1024;
    private final ExecutorService fileIo = Executors.newSingleThreadExecutor();
    private long pendingRequest;
    private boolean pendingExport;
    private String pendingName;
    private volatile Uri exportUri;
    private AlertDialog graphicsFailureDialog;
    private AlertDialog taskStallDialog;
    private final AndroidDiagnostics diagnostics = new AndroidDiagnostics(this);
    private final PerformanceRecorder performanceRecorder = new PerformanceRecorder(this, fileIo);
    private volatile float[] uiMetrics = new float[]{1, 1, 0, 0, 0, 0};

    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        android.view.View decor = getWindow().getDecorView();
        decor.getViewTreeObserver().addOnGlobalLayoutListener(this::updateUiMetrics);
        updateUiMetrics();
        String priorFailure = getSharedPreferences("startup-health", MODE_PRIVATE)
            .getString("graphics-failure", null);
        if (priorFailure != null) decor.post(() -> showFailureDialog(priorFailure));
    }

    private void updateUiMetrics() {
        android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
        android.view.WindowInsets insets = getWindow().getDecorView().getRootWindowInsets();
        int left=0, top=0, right=0, bottom=0;
        if (insets != null) {
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets safe = insets.getInsets(android.view.WindowInsets.Type.systemBars()
                    | android.view.WindowInsets.Type.displayCutout() | android.view.WindowInsets.Type.ime());
                left=safe.left; top=safe.top; right=safe.right; bottom=safe.bottom;
            } else {
                left=insets.getSystemWindowInsetLeft(); top=insets.getSystemWindowInsetTop();
                right=insets.getSystemWindowInsetRight(); bottom=insets.getSystemWindowInsetBottom();
                android.view.DisplayCutout cutout=insets.getDisplayCutout();
                if (cutout != null) {
                    left=Math.max(left,cutout.getSafeInsetLeft()); top=Math.max(top,cutout.getSafeInsetTop());
                    right=Math.max(right,cutout.getSafeInsetRight()); bottom=Math.max(bottom,cutout.getSafeInsetBottom());
                }
            }
        }
        float scaled = android.util.TypedValue.applyDimension(android.util.TypedValue.COMPLEX_UNIT_SP, 18, metrics);
        uiMetrics = new float[]{metrics.density, scaled / (18 * metrics.density), left, top, right, bottom};
    }

    // Immutable snapshot: native render threads never inspect Android Views.
    public float[] getUiMetrics() { return uiMetrics; }

    private static native void prepareNative(String files, String libraries);
    private static native void documentResult(long request, String path, String error);
    static native void beginPerformanceCapture();
    static native String endPerformanceCapture();
    private static native void publishSurface(android.view.Surface surface);
    private static native void memoryPressure(int level);
    @Override public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (!mBrokenLibraries) memoryPressure(level);
    }
    @Override public void onLowMemory() {
        super.onLowMemory();
        if (!mBrokenLibraries) memoryPressure(80);
    }
    public void showPerformanceTools(String ignored) { runOnUiThread(performanceRecorder::show); }
    public String getBuildIdentity() {
        try {
            android.content.pm.PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.versionName + " (Android build " + info.getLongVersionCode() + ")";
        } catch (android.content.pm.PackageManager.NameNotFoundException unavailable) { return "Android build identity unavailable"; }
    }

    // Called by the launcher Support section; no adb or storage permission needed.
    public void exportDiagnostics() {
        runOnUiThread(() -> {
            if (pendingRequest != 0) {
                Toast.makeText(this, "Finish the current file selection first", Toast.LENGTH_LONG).show();
                return;
            }
            diagnostics.start(fileIo);
        });
    }

    // Android's own UI remains available even when the GPU cannot draw ImGui.
    public void showGraphicsFailure(String message) {
        // Persist outside the native renderer: SDL may finish the Activity as
        // its runtime exits. The message remains available on the next launch.
        getSharedPreferences("startup-health", MODE_PRIVATE).edit()
            .putString("graphics-failure", message).apply();
        runOnUiThread(() -> showFailureDialog(message));
    }

    public void showTaskStall(String message) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            if (taskStallDialog != null && taskStallDialog.isShowing()) return;
            taskStallDialog = new AlertDialog.Builder(this).setTitle("Game is taking longer than expected")
                .setMessage(message + "\n\nYou can keep waiting or export logs for diagnosis. This does not reset your settings or saves.")
                .setPositiveButton("Keep waiting", (dialog, which) -> {})
                .setNeutralButton("Export logs", (dialog, which) -> exportDiagnostics())
                .setNegativeButton("Close app...", (dialog, which) ->
                    new AlertDialog.Builder(this).setTitle("Close the stalled game?")
                        .setMessage("Unsaved progress may be lost. Export logs first if you need to report this problem.")
                        .setNegativeButton("Cancel", (confirm, button) -> {})
                        .setPositiveButton("Close app", (confirm, button) -> {
                            // Explicit last-resort exit, never an automatic
                            // timeout. Do not wait for a hung native GPU thread.
                            finishAndRemoveTask();
                            android.os.Process.killProcess(android.os.Process.myPid());
                        }).show()).show();
        });
    }

    private void showFailureDialog(String message) {
            if (isFinishing() || isDestroyed()) return;
            if (graphicsFailureDialog != null && graphicsFailureDialog.isShowing()) return;
            graphicsFailureDialog = new AlertDialog.Builder(this).setTitle("Graphics device stopped")
                .setMessage(message + "\n\nUse Export logs below before trying again. Lower settings can reduce load, but cannot supply missing GPU features.")
                .setPositiveButton("Export logs", (dialog, which) -> exportDiagnostics())
                .setNeutralButton("Close app...", (dialog, which) ->
                    new AlertDialog.Builder(this).setTitle("Close the stopped game?")
                        .setMessage("Unsaved progress may be lost. Your saved games and settings will not be deleted.")
                        .setNegativeButton("Cancel", (confirm, button) -> showFailureDialog(message))
                        .setPositiveButton("Close app", (confirm, button) -> {
                            finishAndRemoveTask();
                            android.os.Process.killProcess(android.os.Process.myPid());
                        }).show())
                .setNegativeButton("Dismiss", (dialog, which) ->
                    getSharedPreferences("startup-health", MODE_PRIVATE).edit()
                        .remove("graphics-failure").apply()).show();
    }

    @Override protected String[] getLibraries() { return new String[]{"c++_shared", "SDL2", "DKR-R"}; }

    @Override protected String[] getArguments() {
        // SDL calls this on its native startup thread, not Android's UI thread.
        try {
            stageRuntimeAssets();
            File config = new File(getFilesDir(), "config");
            if (!config.isDirectory() && !config.mkdirs()) throw new IOException("Cannot create settings directory");
            prepareNative(getFilesDir().getAbsolutePath(), getApplicationInfo().nativeLibraryDir);
            return new String[]{"--config", config.getAbsolutePath()};
        } catch (IOException error) {
            runOnUiThread(() -> new AlertDialog.Builder(this).setTitle("DKR-R could not start")
                .setMessage(error.getMessage()).setCancelable(false)
                .setPositiveButton("Close", (dialog, which) -> finish()).show());
            throw new IllegalStateException("Cannot stage application assets", error);
        }
    }

    private void copyAssets(String source, File target) throws IOException {
        String[] children = getAssets().list(source);
        if (children != null && children.length != 0) {
            if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create asset directory");
            for (String child : children) copyAssets(source + "/" + child, new File(target, child));
        } else {
            android.util.AtomicFile destination = new android.util.AtomicFile(target);
            FileOutputStream output = destination.startWrite();
            try (InputStream input = getAssets().open(source)) {
                copyBounded(input, output);
                destination.finishWrite(output);
            } catch (IOException | RuntimeException failure) {
                destination.failWrite(output);
                throw failure;
            }
        }
    }

    private boolean assetsPresent(String source, File target) throws IOException {
        String[] children = getAssets().list(source);
        if (children != null && children.length != 0) {
            if (!target.isDirectory()) return false;
            for (String child : children)
                if (!assetsPresent(source + "/" + child, new File(target, child))) return false;
            return true;
        }
        return target.isFile() && target.length() > 0;
    }

    private void stageRuntimeAssets() throws IOException {
        String stamp;
        try {
            android.content.pm.PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            stamp = info.getLongVersionCode() + ":" + info.lastUpdateTime;
        } catch (android.content.pm.PackageManager.NameNotFoundException error) {
            throw new IOException("Cannot identify installed build", error);
        }
        File assets = new File(getFilesDir(), "assets");
        android.util.AtomicFile marker = new android.util.AtomicFile(new File(getFilesDir(), "runtime-assets.version"));
        String previous = "";
        try (InputStream input = marker.openRead()) {
            byte[] buffer = new byte[128];
            int size = input.read(buffer);
            if (size > 0) previous = new String(buffer, 0, size, java.nio.charset.StandardCharsets.UTF_8);
        } catch (IOException ignored) { /* First install or interrupted extraction: retry. */ }
        if (stamp.equals(previous) && assetsPresent("runtime", assets)) return;
        copyAssets("runtime", assets);
        FileOutputStream output = marker.startWrite();
        try {
            output.write(stamp.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            marker.finishWrite(output);
        } catch (IOException failure) { marker.failWrite(output); throw failure; }
    }

    // Called by the NFD compatibility adapter on SDL's native thread.
    public void pickDocument(long request, boolean save, String suggestedName) {
        runOnUiThread(() -> {
            if (pendingRequest != 0 || diagnostics.isBusy()) {
                documentResult(request, null, "Another file selection is already open.");
                return;
            }
            pendingRequest = request; pendingExport = save; pendingName = safeName(suggestedName);
            Intent intent = new Intent(save ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/octet-stream");
            if (save) intent.putExtra(Intent.EXTRA_TITLE, pendingName);
            else intent.setType("*/*");
            try { startActivityForResult(intent, PICK_DOCUMENT); }
            catch (RuntimeException error) {
                pendingRequest = 0;
                documentResult(request, null, "No Android file picker is available.");
            }
        });
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (diagnostics.onResult(requestCode, resultCode, data, fileIo)) return;
        if (requestCode != PICK_DOCUMENT || pendingRequest == 0) return;
        final long request = pendingRequest;
        final boolean save = pendingExport;
        final String name = pendingName;
        pendingRequest = 0;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            documentResult(request, null, null); return;
        }
        final Uri uri = data.getData();
        fileIo.execute(() -> {
            File selected = null;
            try {
                File directory = new File(getFilesDir(), "document-staging/" + java.util.UUID.randomUUID());
                if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create import directory");
                selected = new File(directory, save ? name : documentName(uri));
                if (save) exportUri = uri;
                else try (InputStream input = getContentResolver().openInputStream(uri);
                          OutputStream output = new FileOutputStream(selected)) {
                    if (input == null) throw new IOException("Cannot read selected document");
                    copyBounded(input, output);
                }
                documentResult(request, selected.getAbsolutePath(), null);
            } catch (Exception error) {
                if (selected != null) selected.delete();
                documentResult(request, null, "File selection failed: " + error.getMessage());
            }
        });
    }

    public void finishExport(String path) {
        final Uri destination = exportUri;
        exportUri = null;
        if (destination == null) return;
        fileIo.execute(() -> {
            try (InputStream input = new FileInputStream(path);
                 OutputStream output = getContentResolver().openOutputStream(destination, "wt")) {
                if (output == null) throw new IOException("Cannot write selected document");
                copyBounded(input, output);
                runOnUiThread(() -> Toast.makeText(this, "Export completed", Toast.LENGTH_LONG).show());
            } catch (Exception error) {
                runOnUiThread(() -> new AlertDialog.Builder(this).setTitle("Export failed")
                    .setMessage(error.getMessage()).setPositiveButton("OK", null).show());
            }
        });
    }

    private String documentName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return safeName(cursor.getString(0));
        }
        return "import.bin";
    }
    private static String safeName(String name) {
        if (name == null || name.isEmpty()) return "import.bin";
        name = name.replaceAll("[\\\\/\\p{Cntrl}]", "_");
        if (name.equals(".") || name.equals("..")) return "import.bin";
        return name.length() > 180 ? name.substring(name.length() - 180) : name;
    }
    private static void copyBounded(InputStream input, OutputStream output) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        long total = 0;
        for (int length; (length = input.read(buffer)) != -1;) {
            total += length;
            if (total > MAX_IMPORT_BYTES) throw new IOException("Document exceeds the 1 GiB import limit");
            output.write(buffer, 0, length);
        }
    }
    @Override protected void onDestroy() {
        performanceRecorder.destroy();
        if (pendingRequest != 0) documentResult(pendingRequest, null, null);
        pendingRequest = 0;
        fileIo.shutdown();
        super.onDestroy();
    }
}
