package io.github.thatguymcd.dkrr;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Bounded startup/tail capture, kept independent of Android for host tests. */
final class DiagnosticLogSnapshot {
    static void write(ZipOutputStream zip, String entry, File file, int limit) throws IOException {
        if (limit < 4) throw new IllegalArgumentException("Log limit must be at least four bytes");
        if (!file.isFile()) return;
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long size = input.length();
            if (size <= limit) {
                range(zip, entry, input, 0, (int)size);
            } else {
                // A GPU error flood must not discard the original startup failure.
                int head = limit / 4;
                int tail = limit - head;
                range(zip, entry + ".head", input, 0, head);
                range(zip, entry + ".tail", input, size - tail, tail);
                zip.putNextEntry(new ZipEntry(entry + ".ranges.txt"));
                zip.write(("Original bytes: " + size + "\nHead: [0, " + head
                    + ")\nTail: [" + (size - tail) + ", " + size
                    + ")\nMiddle omitted to bound export size.\n").getBytes(StandardCharsets.UTF_8));
                zip.closeEntry();
            }
        }
    }

    private static void range(ZipOutputStream zip, String entry, RandomAccessFile input,
                              long offset, int length) throws IOException {
        input.seek(offset);
        byte[] data = new byte[length];
        input.readFully(data);
        zip.putNextEntry(new ZipEntry(entry)); zip.write(data); zip.closeEntry();
    }
}
