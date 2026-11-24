package industrial.einhorn.netkit.shell;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * One TCP connect attempt per call, answered with a Codes value: that is what the scanner wasm asks for, one probe at a time
 * (nk_scan_next -> probe -> nk_scan_result). Also the Ping tool. Events: {"t":"probe","job":N,"code":C,"ms":M}.
 * Jobs at 1,000,000 and above are the user's own Ping / Port-check taps rather than part of a sweep; the shell does not care.
 */
public final class Prober {
    private final Sink sink;
    private final ExecutorService pool = Executors.newFixedThreadPool(64, r -> { Thread t = new Thread(r, "netkit-probe"); t.setDaemon(true); return t; });

    public Prober(Sink sink) { this.sink = sink; }

    public void probe(final int job, final String host, final int port, final int timeoutMs) {
        pool.execute(() -> {
            long t0 = System.nanoTime();
            int code = attempt(host, port, timeoutMs);
            sink.event(Json.obj("t", "probe", "job", job, "code", code, "ms", (int) ((System.nanoTime() - t0) / 1_000_000)));
        });
    }

    /** Is something there? ICMP when the platform allows it, else a few TCP connects: an answer or a refusal both mean "up". */
    public void ping(final int job, final String host, final int timeoutMs) {
        pool.execute(() -> {
            long t0 = System.nanoTime();
            int code = Codes.TIMEOUT;
            try {
                InetAddress a = InetAddress.getByName(host);
                if (a.isReachable(timeoutMs)) code = Codes.OPEN;
                else for (int p : new int[] {7, 22, 80, 443, 445}) {
                    int c = attempt(host, p, Math.max(300, timeoutMs / 5));
                    if (c == Codes.OPEN || c == Codes.REFUSED) { code = Codes.OPEN; break; }
                    if (c == Codes.UNREACHABLE) code = Codes.UNREACHABLE;
                }
            } catch (IOException e) {
                code = Codes.of(e);
            }
            sink.event(Json.obj("t", "probe", "job", job, "code", code, "ms", (int) ((System.nanoTime() - t0) / 1_000_000)));
        });
    }

    static int attempt(String host, int port, int timeoutMs) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), Math.max(1, timeoutMs));
            return Codes.OPEN;
        } catch (IOException e) {
            return Codes.of(e);
        } catch (RuntimeException e) {
            return Codes.OTHER;
        }
    }

    public void shutdown() { pool.shutdownNow(); }
}
