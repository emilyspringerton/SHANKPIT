package industrial.einhorn.netkit.shell;

import java.io.IOException;
import java.net.ConnectException;
import java.net.NoRouteToHostException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;

/**
 * The one piece of vocabulary the shell shares with the PARENA wasm (stdlib/netkit/nk_scan.prn): what happened to one connect attempt.
 *   0 connected   1 refused (the host is up, nothing listens)   2 timed out   3 unreachable (no route, host down, unknown name)   4 other
 * The shell only classifies; what each code MEANS (a device found, a sentence for the user) is decided in the wasm.
 */
public final class Codes {
    public static final int OPEN = 0, REFUSED = 1, TIMEOUT = 2, UNREACHABLE = 3, OTHER = 4;
    private Codes() { }

    public static int of(IOException e) {
        if (e instanceof SocketTimeoutException) return TIMEOUT;
        if (e instanceof NoRouteToHostException || e instanceof UnknownHostException) return UNREACHABLE;
        String m = String.valueOf(e.getMessage()).toLowerCase(java.util.Locale.ROOT);
        if (e instanceof ConnectException) {
            if (m.contains("refused")) return REFUSED;
            if (m.contains("timed out") || m.contains("timeout")) return TIMEOUT;
            if (m.contains("unreachable") || m.contains("no route")) return UNREACHABLE;
            return OTHER;
        }
        if (m.contains("refused")) return REFUSED;
        if (m.contains("unreachable") || m.contains("no route")) return UNREACHABLE;
        if (m.contains("timed out") || m.contains("timeout")) return TIMEOUT;
        return OTHER;
    }
}
