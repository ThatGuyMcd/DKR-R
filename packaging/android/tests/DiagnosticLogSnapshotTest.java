package io.github.thatguymcd.dkrr;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/** Run with the host JDK, no phone, Android framework mocks, or network needed. */
public final class DiagnosticLogSnapshotTest {
    private static Map<String, byte[]> capture(File source, int cap) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            DiagnosticLogSnapshot.write(zip, "runtime.log", source, cap);
        }
        Map<String, byte[]> result = new HashMap<>();
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(bytes.toByteArray()))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                result.put(entry.getName(), zip.readAllBytes());
            }
        }
        return result;
    }
    private static void require(boolean condition) {
        if (!condition) throw new AssertionError("Diagnostic snapshot mismatch");
    }
    public static void main(String[] args) throws Exception {
        Path temp = Files.createTempDirectory("dkr-log-snapshot-test-");
        Path source = temp.resolve("runtime.log");
        try {
            require(capture(source.toFile(), 1024).isEmpty());
            for (int size : new int[]{0, 1, 1024, 1025, 5 * 1024 * 1024}) {
                byte[] data = new byte[size];
                new Random(49).nextBytes(data);
                Files.write(source, data);
                Map<String, byte[]> entries = capture(source.toFile(), 1024);
                if (size <= 1024) {
                    require(entries.size() == 1);
                    require(Arrays.equals(data, entries.get("runtime.log")));
                } else {
                    require(entries.size() == 3);
                    require(Arrays.equals(Arrays.copyOfRange(data, 0, 256), entries.get("runtime.log.head")));
                    require(Arrays.equals(Arrays.copyOfRange(data, size - 768, size), entries.get("runtime.log.tail")));
                    require(new String(entries.get("runtime.log.ranges.txt"), java.nio.charset.StandardCharsets.UTF_8)
                        .contains("Original bytes: " + size));
                }
                require(Arrays.equals(data, Files.readAllBytes(source)));
            }
            System.out.println("PASS: missing, empty, small, exact-cap, oversized and error-flood logs; source unchanged.");
        } finally {
            Files.deleteIfExists(source);
            Files.deleteIfExists(temp);
        }
    }
}
