package industrial.einhorn.netkit.core;

/**
 * View-only RFB client (VS0: no pointer/key events are ever sent).
 * Security: None + VNC Authentication (DES). Encodings: Raw, CopyRect, Hextile (what TightVNC speaks happily).
 */
public interface VncSession extends AutoCloseable {
    interface Listener {
        void onSize(int width, int height, String desktopName);
        /** argb is width*height opaque pixels (0xFFRRGGBB), row-major. CopyRect is resolved by the session. */
        void onRect(int x, int y, int w, int h, int[] argb);
        void onClosed(String reason);
    }
    @Override void close();

    /** Blocking connect+handshake+auth, then frame updates stream on a background thread. */
    static VncSession open(String host, int port, String password, int timeoutMs, Listener l) throws NetkitException {
        return VncSessionImpl.open(host, port, password, timeoutMs, l);
    }
}
