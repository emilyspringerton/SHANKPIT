package industrial.einhorn.netkit.core;

import java.util.List;

/** LAN scanner: ARP-table read + parallel TCP probes of the interesting ports. */
public final class Scanner {
    public interface Listener {
        void onDevice(Device d);                 // may be called more than once per ip (updates)
        void onDone(int hostsChecked);
    }
    /**
     * @param cidr "192.168.4.0/24", or null to auto-detect from the active IPv4 interface
     * @param ports ports to probe per host (see DEFAULT_PORTS)
     * Blocking; call from a worker thread. cancel() makes it return early.
     */
    public static final int[] DEFAULT_PORTS = {22, 80, 443, 445, 3389, 5900, 5901, 8080};

    public void scan(String cidr, int[] ports, int timeoutMs, Listener l) throws NetkitException {
        throw new UnsupportedOperationException("stub");
    }
    public void cancel() { }
    /** Local IPv4 addresses with prefix length, e.g. "192.168.4.23/24" (used for auto-detect). */
    public static List<String> localCidrs() { throw new UnsupportedOperationException("stub"); }
}
