package industrial.einhorn.netkit.shell;

/** Just enough JSON to build the shell's events: a quoted string and an object writer. Names from the network are untrusted text. */
public final class Json {
    private Json() { }

    public static String quote(String s) {
        StringBuilder sb = new StringBuilder(s.length() + 2).append('"');
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                case ' ': sb.append("\\u2028"); break;     // legal in JSON, a line terminator in JavaScript source
                case ' ': sb.append("\\u2029"); break;
                default:
                    if (c < 0x20 || c == 0x7f) sb.append(String.format("\\u%04x", (int) c)); else sb.append(c);
            }
        }
        return sb.append('"').toString();
    }

    /** {"t":"tcp","id":3,"e":"open"} style objects: alternating key, value (Integer/Long -> number, anything else -> string). */
    public static String obj(Object... kv) {
        StringBuilder sb = new StringBuilder("{");
        for (int i = 0; i < kv.length; i += 2) {
            if (i > 0) sb.append(',');
            sb.append(quote(String.valueOf(kv[i]))).append(':');
            Object v = kv[i + 1];
            if (v instanceof Integer || v instanceof Long) sb.append(v); else sb.append(quote(String.valueOf(v)));
        }
        return sb.append('}').toString();
    }
}
