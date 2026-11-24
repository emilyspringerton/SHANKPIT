package industrial.einhorn.netkit.shell;

import java.util.ArrayList;
import java.util.List;

/** A Sink that remembers events and lets a test wait for one. */
final class Events implements Sink {
    final List<String> all = new ArrayList<>();
    @Override public synchronized void event(String json) { all.add(json); notifyAll(); }
    synchronized List<String> copy() { return new ArrayList<>(all); }

    /** Wait until some event contains every fragment (or the time runs out); returns it, or null. */
    synchronized String await(long ms, String... fragments) throws InterruptedException {
        long end = System.currentTimeMillis() + ms;
        while (true) {
            for (String e : all) { boolean ok = true; for (String f : fragments) ok &= e.contains(f); if (ok) return e; }
            long left = end - System.currentTimeMillis();
            if (left <= 0) return null;
            wait(left);
        }
    }
}
