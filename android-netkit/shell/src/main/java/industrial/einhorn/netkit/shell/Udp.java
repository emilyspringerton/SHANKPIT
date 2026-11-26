package industrial.einhorn.netkit.shell;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * One UDP question, one answer: the scanner's name lookups (mDNS reverse lookup on 5353, NetBIOS node status on 137). The wasm builds
 * the packet and parses the reply (PARENA stdlib/netkit/nk_names.prn); this class only sends bytes and hands back the first datagram
 * that comes from the address asked. Events: {"t":"udp","id":N,"e":"data","b":base64} or {"t":"udp","id":N,"e":"none"} (no answer in
 * time, or an error -- for a name lookup those mean the same thing).
 *
 * Unicast only, from an ordinary port: no multicast lock, no broadcast, nothing sent anywhere but the device being named.
 */
public final class Udp {
    static final int MAX_REPLY = 1536;     // the wasm's packet buffer (nk-pkt-cap)
    private final Sink sink;
    private final ExecutorService pool = Executors.newFixedThreadPool(8, r -> { Thread t = new Thread(r, "netkit-udp"); t.setDaemon(true); return t; });

    public Udp(Sink sink) { this.sink = sink; }

    public void query(final int id, final String host, final int port, final String base64, final int timeoutMs) {
        pool.execute(() -> {
            byte[] reply = ask(host, port, B64.decode(base64), timeoutMs);
            if (reply == null) sink.event(Json.obj("t", "udp", "id", id, "e", "none"));
            else sink.event(Json.obj("t", "udp", "id", id, "e", "data", "b", B64.encode(reply, 0, reply.length)));
        });
    }

    /** The first reply from host:port within the timeout, or null. Datagrams from anyone else are ignored (not an answer to us). */
    static byte[] ask(String host, int port, byte[] packet, int timeoutMs) {
        try (DatagramSocket s = new DatagramSocket()) {
            InetAddress to = InetAddress.getByName(host);
            s.send(new DatagramPacket(packet, packet.length, new InetSocketAddress(to, port)));
            long end = System.currentTimeMillis() + Math.max(1, timeoutMs);
            byte[] buf = new byte[MAX_REPLY];
            while (true) {
                long left = end - System.currentTimeMillis();
                if (left <= 0) return null;
                s.setSoTimeout((int) left);
                DatagramPacket in = new DatagramPacket(buf, buf.length);
                try {
                    s.receive(in);
                } catch (SocketTimeoutException e) {
                    return null;
                }
                if (to.equals(in.getAddress())) {
                    byte[] out = new byte[in.getLength()];
                    System.arraycopy(buf, 0, out, 0, out.length);
                    return out;
                }
            }
        } catch (Exception e) {
            return null;    // unknown host, no route, a refused port (ICMP), a closed socket: no name today
        }
    }

    public void shutdown() { pool.shutdownNow(); }
}
