package industrial.einhorn.netkit.shell;

import java.net.InetAddress;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Best-effort reverse DNS for a device that was just found. Event: {"t":"name","ip":"a.b.c.d","name":"host.lan"}; silence when there is none. */
public final class Resolver {
    private final Sink sink;
    private final ExecutorService pool = Executors.newFixedThreadPool(4, r -> { Thread t = new Thread(r, "netkit-resolve"); t.setDaemon(true); return t; });

    public Resolver(Sink sink) { this.sink = sink; }

    public void resolve(final String ip) {
        pool.execute(() -> {
            try {
                String name = InetAddress.getByName(ip).getCanonicalHostName();
                if (name != null && !name.equals(ip) && !name.isEmpty()) sink.event(Json.obj("t", "name", "ip", ip, "name", name));
            } catch (Exception ignored) {
                // no name is a normal answer
            }
        });
    }

    public void shutdown() { pool.shutdownNow(); }
}
