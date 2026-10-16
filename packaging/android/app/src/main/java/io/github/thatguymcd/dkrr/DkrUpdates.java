package io.github.thatguymcd.dkrr;

import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.provider.Settings;
import java.io.*;
import java.net.URL;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import javax.net.ssl.HttpsURLConnection;

/** System HTTPS trust and user-confirmed APK installation; never uninstall. */
final class DkrUpdates {
    private static native boolean cancelled(long context);
    private static native void status(long context, int code);
    private static native boolean header(long context, String name, String value);
    private static native boolean bytes(long context, byte[] data, int count);

    private static boolean allowed(URL url) {
        String host = url.getHost();
        return url.getProtocol().equals("https") && url.getUserInfo() == null
            && (url.getPort() == -1 || url.getPort() == 443)
            && (host.equals("api.github.com") || host.equals("github.com")
                || host.equals("release-assets.githubusercontent.com") || host.equals("objects.githubusercontent.com"));
    }

    static String fetch(String address, String etag, long offset, long context) {
        try {
            URL url = new URL(address);
            for (int redirects = 0; redirects < 6; redirects++) {
                if (cancelled(context)) return "Update request paused.";
                if (!allowed(url)) return "Update redirect is not an approved HTTPS endpoint.";
                HttpsURLConnection connection = (HttpsURLConnection) url.openConnection();
                try {
                    connection.setInstanceFollowRedirects(false);
                    connection.setConnectTimeout(4000);
                    connection.setReadTimeout(4000);
                    connection.setRequestProperty("User-Agent", "DKR-R-Updater/1");
                    connection.setRequestProperty("Accept", "application/vnd.github+json");
                    connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
                    if (!etag.isEmpty() && !etag.contains("\r") && !etag.contains("\n"))
                        connection.setRequestProperty(offset > 0 ? "If-Range" : "If-None-Match", etag);
                    if (offset > 0) connection.setRequestProperty("Range", "bytes=" + offset + "-");
                    int code = connection.getResponseCode();
                    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
                        String location = connection.getHeaderField("Location");
                        if (location == null) return "Update redirect has no destination.";
                        url = new URL(url, location);
                        continue;
                    }
                    status(context, code);
                    int headerBytes = 0;
                    for (Map.Entry<String, List<String>> field : connection.getHeaderFields().entrySet()) {
                        if (field.getKey() == null) continue;
                        for (String value : field.getValue()) {
                            headerBytes += field.getKey().length() + value.length();
                            if (headerBytes > 32768 || !header(context, field.getKey(), value))
                                return "Update response headers are too large.";
                        }
                    }
                    if (code != 200 && code != 206) return "";
                    try (InputStream input = connection.getInputStream()) {
                        byte[] buffer = new byte[65536];
                        for (int n; (n = input.read(buffer)) != -1;) {
                            if (cancelled(context) || !bytes(context, buffer, n)) return "Update transfer paused or exceeded its limits.";
                        }
                    }
                    return "";
                } finally { connection.disconnect(); }
            }
            return "Update server redirected too many times.";
        } catch (Exception error) {
            // Do not expose signed CDN URLs or personal paths in diagnostics.
            return "Android update connection interrupted or TLS verification failed. Nothing was installed.";
        }
    }

    private static Set<String> certificates(PackageInfo info) throws Exception {
        Set<String> identities = new HashSet<>();
        if (info.signingInfo == null) throw new IOException("Missing signing identity");
        for (Signature signature : info.signingInfo.getApkContentsSigners()) {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(signature.toByteArray());
            StringBuilder text = new StringBuilder();
            for (byte b : digest) text.append(String.format(Locale.ROOT, "%02x", b & 255));
            identities.add(text.toString());
        }
        return identities;
    }

    private static boolean upgradeSigningIdentity(PackageInfo current, PackageInfo next) throws Exception {
        Set<String> installed = certificates(current);
        if (installed.equals(certificates(next))) return true;
        // PackageManager verifies APK signing lineage. Accept an authorised
        // single-signer rotation, never an unrelated key or multi-signer change.
        if (current.signingInfo.hasMultipleSigners() || next.signingInfo.hasMultipleSigners()) return false;
        for (Signature signer : next.signingInfo.getSigningCertificateHistory()) {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(signer.toByteArray());
            StringBuilder value = new StringBuilder();
            for (byte b : digest) value.append(String.format(Locale.ROOT, "%02x", b & 255));
            if (installed.contains(value.toString())) return true;
        }
        return false;
    }

    static String install(DkrActivity activity, String source, String version, String sha) {
        File directory = new File(activity.getCacheDir(), "updates");
        File temporary = new File(directory, "pending-update.apk");
        File apk = new File(directory, "update.apk");
        try {
            if (!directory.isDirectory() && !directory.mkdirs()) return "Could not create the private APK staging directory.";
            MessageDigest hash = MessageDigest.getInstance("SHA-256");
            long total = 0;
            try (InputStream input = new FileInputStream(source); FileOutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[65536];
                for (int n; (n = input.read(buffer)) != -1;) {
                    total += n;
                    if (total > 512L * 1024 * 1024) throw new IOException("APK too large");
                    output.write(buffer, 0, n); hash.update(buffer, 0, n);
                }
                output.getFD().sync();
            }
            StringBuilder actual = new StringBuilder();
            for (byte b : hash.digest()) actual.append(String.format(Locale.ROOT, "%02x", b & 255));
            if (!actual.toString().equals(sha)) return "The APK changed during installation staging. Nothing was installed.";
            PackageManager packages = activity.getPackageManager();
            PackageInfo next = packages.getPackageArchiveInfo(temporary.getAbsolutePath(), PackageManager.GET_SIGNING_CERTIFICATES);
            PackageInfo current = packages.getPackageInfo(activity.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
            if (next == null || !activity.getPackageName().equals(next.packageName) || !version.equals(next.versionName))
                return "The update APK does not match this DKR-R application/version. Your existing app and saves were retained.";
            if (next.getLongVersionCode() <= current.getLongVersionCode())
                return "This APK does not have a newer Android version code. Your existing app was retained.";
            if (next.applicationInfo != null && next.applicationInfo.minSdkVersion > android.os.Build.VERSION.SDK_INT)
                return "This update requires a newer Android version. Your existing application was retained.";
            if (!upgradeSigningIdentity(current, next))
                return "The APK signing identity differs from your installed app. Do not uninstall to work around this; export your saves and contact the release maintainer.";
            Files.move(temporary.toPath(), apk.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            if (!packages.canRequestPackageInstalls()) {
                activity.runOnUiThread(() -> activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + activity.getPackageName()))));
                return "Allow DKR-R to install updates in Android's settings, then return and choose Install again. Nothing has been uninstalled.";
            }
            Uri uri = Uri.parse("content://" + activity.getPackageName() + ".updates/update.apk");
            activity.runOnUiThread(() -> {
                try {
                    Intent intent = new Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    activity.startActivity(intent);
                } catch (Exception failure) {
                    android.widget.Toast.makeText(activity, "Android could not open the APK installer. The current app was kept.", android.widget.Toast.LENGTH_LONG).show();
                }
            });
            return "";
        } catch (Exception failure) {
            return "Android could not safely stage this APK. The current application and its data were retained.";
        } finally { if (temporary.isFile()) temporary.delete(); }
    }
}
