package industrial.einhorn.netkit.shell;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * The kernel's ARP cache as text (/proc/net/arp), or "" when it cannot be read (Android 10+ blocks it for apps). The wasm parses it
 * (nk_scan_arp); with or without it the scan still finds devices, ARP only adds MAC addresses and hosts that never answer a port.
 */
public final class ArpTable {
    private ArpTable() { }

    public static String read() { return read("/proc/net/arp"); }

    static String read(String path) {
        try (InputStream in = new FileInputStream(path)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] b = new byte[4096];
            int n, total = 0;
            while ((n = in.read(b)) > 0 && total < 60000) { out.write(b, 0, n); total += n; }
            return new String(out.toByteArray(), java.nio.charset.StandardCharsets.UTF_8);
        } catch (IOException | SecurityException e) {
            return "";
        }
    }
}
