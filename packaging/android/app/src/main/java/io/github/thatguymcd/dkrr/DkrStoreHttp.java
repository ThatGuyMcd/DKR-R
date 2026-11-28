package io.github.thatguymcd.dkrr;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;

final class DkrStoreHttp {
    private static native boolean cancelled(long context);
    private static native void status(long context, int code);
    private static native boolean header(long context, String name, String value);
    private static native boolean bytes(long context, byte[] buffer, int count);

    static boolean request(String url, String method, byte[] body, String[] headers, int timeout, long context) {
        HttpsURLConnection connection = null;
        try {
            URL endpoint = new URL(url);
            if (!endpoint.getProtocol().equals("https") || cancelled(context)) return false;
            connection = (HttpsURLConnection) endpoint.openConnection();
            connection.setUseCaches(false);
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(4000);
            connection.setReadTimeout(timeout);
            connection.setRequestMethod(method);
            for (int i = 0; i < headers.length; i += 2)
                connection.setRequestProperty(headers[i], headers[i + 1]);
            if (method.equals("POST") || method.equals("PUT")) {
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(body.length);
                try (OutputStream out = connection.getOutputStream()) {
                    for (int offset = 0; offset < body.length; offset += 65536) {
                        if (cancelled(context)) return false;
                        out.write(body, offset, Math.min(65536, body.length - offset));
                    }
                }
            }
            int code = connection.getResponseCode();
            if (code < 100) return false;
            status(context, code);
            for (int i = 1; ; i++) {
                String name = connection.getHeaderFieldKey(i);
                String value = connection.getHeaderField(i);
                if (name == null && value == null) break;
                if (name != null && value != null && !header(context, name, value)) return false;
            }
            try (InputStream in = code >= 400 ? connection.getErrorStream() : connection.getInputStream()) {
                if (in == null) return true;
                byte[] buffer = new byte[65536];
                while (!cancelled(context)) {
                    int count = in.read(buffer);
                    if (count < 0) return true;
                    if (count > 0 && !bytes(context, buffer, count)) return false;
                }
                return false;
            }
        } catch (Exception failure) {
            return false;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }
}
