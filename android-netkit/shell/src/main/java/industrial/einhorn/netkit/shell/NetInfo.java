package industrial.einhorn.netkit.shell;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * "Which network am I on?": the tablet's own IPv4 addresses on Wi-Fi / Ethernet-like interfaces, and the range each belongs to.
 * Cellular interfaces are skipped (scanning the carrier network is neither useful nor polite). The result is JSON for the page:
 *   {"cidrs":["192.168.4.0/24"],"ips":["192.168.4.23"]}
 * The wasm decides what to scan from the first range, clamps anything wider than /22, and leaves the tablet's own address out.
 */
public final class NetInfo {
    private NetInfo() { }

    public static String json() {
        List<String> cidrs = new ArrayList<>(), ips = new ArrayList<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback() || ni.isVirtual() || cellular(ni.getName())) continue;
                for (InterfaceAddress ia : ni.getInterfaceAddresses()) {
                    InetAddress a = ia.getAddress();
                    if (!(a instanceof Inet4Address) || a.isLinkLocalAddress()) continue;
                    int prefix = ia.getNetworkPrefixLength();
                    if (prefix <= 0 || prefix > 32) continue;
                    ips.add(a.getHostAddress());
                    cidrs.add(cidr(a.getAddress(), prefix));
                }
            }
        } catch (java.net.SocketException ignored) {
            // no interfaces: the page says "No network"
        }
        StringBuilder sb = new StringBuilder("{\"cidrs\":[");
        for (int i = 0; i < cidrs.size(); i++) sb.append(i > 0 ? "," : "").append(Json.quote(cidrs.get(i)));
        sb.append("],\"ips\":[");
        for (int i = 0; i < ips.size(); i++) sb.append(i > 0 ? "," : "").append(Json.quote(ips.get(i)));
        return sb.append("]}").toString();
    }

    static boolean cellular(String name) {
        String n = name.toLowerCase(java.util.Locale.ROOT);
        return n.startsWith("rmnet") || n.startsWith("ccmni") || n.startsWith("pdp") || n.startsWith("v4-rmnet") || n.startsWith("clat") || n.startsWith("tun") || n.startsWith("ppp");
    }

    /** "a.b.c.d/p" for the network containing addr (host bits cleared). */
    public static String cidr(byte[] addr, int prefix) {
        long ip = 0;
        for (int i = 0; i < 4; i++) ip = ip << 8 | (addr[i] & 255);
        long mask = prefix == 0 ? 0 : (0xFFFFFFFFL << (32 - prefix)) & 0xFFFFFFFFL;
        long net = ip & mask;
        return (net >> 24 & 255) + "." + (net >> 16 & 255) + "." + (net >> 8 & 255) + "." + (net & 255) + "/" + prefix;
    }
}
