package industrial.einhorn.netkit.shell;

/**
 * Plain base64 (RFC 4648, no line breaks). The JDK's java.util.Base64 needs Android API 26 and this app supports 24; android.util.Base64
 * is not available to plain-JVM tests. Twenty lines beat either problem.
 */
public final class B64 {
    private static final char[] ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".toCharArray();
    private static final int[] INV = new int[128];
    static { java.util.Arrays.fill(INV, -1); for (int i = 0; i < ALPHA.length; i++) INV[ALPHA[i]] = i; }
    private B64() { }

    public static String encode(byte[] b, int off, int len) {
        StringBuilder sb = new StringBuilder((len + 2) / 3 * 4);
        for (int i = off; i < off + len; i += 3) {
            int n = Math.min(3, off + len - i);
            int v = (b[i] & 255) << 16 | (n > 1 ? (b[i + 1] & 255) << 8 : 0) | (n > 2 ? b[i + 2] & 255 : 0);
            sb.append(ALPHA[v >> 18 & 63]).append(ALPHA[v >> 12 & 63]);
            sb.append(n > 1 ? ALPHA[v >> 6 & 63] : '=').append(n > 2 ? ALPHA[v & 63] : '=');
        }
        return sb.toString();
    }

    public static byte[] decode(String s) {
        int len = s.length();
        while (len > 0 && s.charAt(len - 1) == '=') len--;
        byte[] out = new byte[len * 3 / 4];
        int acc = 0, bits = 0, o = 0;
        for (int i = 0; i < len; i++) {
            char c = s.charAt(i);
            int v = c < 128 ? INV[c] : -1;
            if (v < 0) throw new IllegalArgumentException("not base64: " + (int) c + " at " + i);
            acc = acc << 6 | v; bits += 6;
            if (bits >= 8) { bits -= 8; out[o++] = (byte) (acc >> bits & 255); }
        }
        return o == out.length ? out : java.util.Arrays.copyOf(out, o);
    }
}
