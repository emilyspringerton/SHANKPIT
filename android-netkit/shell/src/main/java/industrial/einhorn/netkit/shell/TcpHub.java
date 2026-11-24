package industrial.einhorn.netkit.shell;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Raw TCP for the page: open a connection, move bytes, close it. That is ALL it does. SSH, VNC, the terminal and every decision live in
 * the PARENA wasm modules; the bytes cross here as base64 and are never interpreted.
 *
 * Events (to the Sink), id = the page's connection id:
 *   {"t":"tcp","id":N,"e":"open"}   {"e":"data","b":"<base64>"}   {"e":"closed"}   {"e":"error","code":1..4}   (codes: see Codes)
 * Writes are queued per connection on one thread, so bytes leave in the order the page wrote them even while the connect is in flight.
 */
public final class TcpHub {
    private static final class Conn {
        final int id;
        volatile Socket socket;
        final ExecutorService writer = Executors.newSingleThreadExecutor(r -> daemon(r, "netkit-tcp-write"));
        volatile boolean closed;
        Conn(int id) { this.id = id; }
    }

    private final Sink sink;
    private final Map<Integer, Conn> conns = new ConcurrentHashMap<>();
    private final ExecutorService readers = Executors.newCachedThreadPool(r -> daemon(r, "netkit-tcp"));

    public TcpHub(Sink sink) { this.sink = sink; }

    private static Thread daemon(Runnable r, String name) { Thread t = new Thread(r, name); t.setDaemon(true); return t; }

    public void open(final int id, final String host, final int port, final int timeoutMs) {
        final Conn c = new Conn(id);
        Conn old = conns.put(id, c);
        if (old != null) shut(old);
        readers.execute(() -> {
            Socket s = new Socket();
            try {
                s.setTcpNoDelay(true);
                s.setKeepAlive(true);
                s.connect(new InetSocketAddress(host, port), Math.max(1, timeoutMs));
            } catch (IOException e) {
                try { s.close(); } catch (IOException ignored) { }
                if (!c.closed) { conns.remove(id, c); sink.event(Json.obj("t", "tcp", "id", id, "e", "error", "code", Codes.of(e))); }
                return;
            } catch (RuntimeException e) {                              // e.g. an illegal port: the page validated it, but never crash the pool
                try { s.close(); } catch (IOException ignored) { }
                if (!c.closed) { conns.remove(id, c); sink.event(Json.obj("t", "tcp", "id", id, "e", "error", "code", Codes.OTHER)); }
                return;
            }
            c.socket = s;
            if (c.closed) { try { s.close(); } catch (IOException ignored) { } return; }
            sink.event(Json.obj("t", "tcp", "id", id, "e", "open"));
            byte[] buf = new byte[16384];
            try {
                InputStream in = s.getInputStream();
                int n;
                while ((n = in.read(buf)) >= 0) {
                    if (n > 0) sink.event(Json.obj("t", "tcp", "id", id, "e", "data", "b", B64.encode(buf, 0, n)));
                }
            } catch (IOException ignored) {
                // a reset or a local close ends the stream the same way: the page is told it is closed
            }
            if (conns.remove(id, c)) {
                shut(c);
                sink.event(Json.obj("t", "tcp", "id", id, "e", "closed"));
            }
        });
    }

    public void write(final int id, final String base64) {
        final Conn c = conns.get(id);
        if (c == null || c.closed) return;
        final byte[] data;
        try { data = B64.decode(base64); } catch (IllegalArgumentException e) { return; }
        c.writer.execute(() -> {
            // wait (briefly) for the connect that is still in flight; the writer thread is this connection's alone
            long until = System.currentTimeMillis() + 15000;
            while (c.socket == null && !c.closed && System.currentTimeMillis() < until) {
                try { Thread.sleep(5); } catch (InterruptedException ie) { return; }
            }
            Socket s = c.socket;
            if (s == null || c.closed) return;
            try {
                OutputStream out = s.getOutputStream();
                out.write(data);
                out.flush();
            } catch (IOException e) {
                try { s.close(); } catch (IOException ignored) { }       // the reader thread reports "closed"
            }
        });
    }

    public void close(int id) {
        Conn c = conns.remove(id);
        if (c != null) shut(c);
    }

    public void closeAll() {
        for (Integer id : conns.keySet()) close(id);
        readers.shutdownNow();
    }

    public int openCount() { return conns.size(); }

    private static void shut(Conn c) {
        c.closed = true;
        c.writer.shutdown();
        Socket s = c.socket;
        if (s != null) try { s.close(); } catch (IOException ignored) { }
    }
}
