package industrial.einhorn.netkit.core;

/** Interactive shell over SSH with a pty. All callbacks arrive on a background thread. */
public interface SshSession extends AutoCloseable {
    interface Listener {
        void onData(byte[] data, int len);        // remote -> terminal (stdout+stderr merged by pty)
        void onClosed(String reason);              // remote exit / network drop; reason may be ""
    }
    /** Params for open(). hostKey policy v0: trust-on-first-use is the caller's job, core accepts all. */
    final class Params {
        public String host; public int port = 22;
        public String user; public String password;
        public int cols = 80, rows = 24;
        public int connectTimeoutMs = 8000;
    }
    void write(byte[] data) throws NetkitException;
    void resize(int cols, int rows);
    @Override void close();

    /** Blocking connect+auth+shell. Throws NetkitException with a precise ErrorKind. */
    static SshSession open(Params p, Listener l) throws NetkitException { return SshSessionImpl.open(p, l); }
}
