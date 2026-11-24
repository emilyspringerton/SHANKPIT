package industrial.einhorn.netkit.core;

/** Port check: connect to host:port and classify the outcome into an ErrorKind. */
public final class Probe {
    public static final class Result {
        public boolean open;
        public ErrorKind kind;   // null when open
        public String message;
        public long millis;
    }
    public static Result tcp(String host, int port, int timeoutMs) { throw new UnsupportedOperationException("stub"); }
    /** ICMP-or-TCP-echo style reachability: "is the host there at all". */
    public static Result ping(String host, int timeoutMs) { throw new UnsupportedOperationException("stub"); }
}
