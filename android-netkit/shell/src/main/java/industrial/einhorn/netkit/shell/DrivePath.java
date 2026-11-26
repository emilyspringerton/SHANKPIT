package industrial.einhorn.netkit.shell;

import java.util.ArrayList;
import java.util.List;

/**
 * A path on the USB drive as the page names it: "" is the drive's top folder, "kit/setup.bat" a file one folder down. Every component is
 * checked here before the shell asks Android's storage framework for it: no "." or "..", no empty parts, nothing FAT/exFAT or Windows
 * cannot store (\ / : * ? " < > | and control characters), no trailing dot or space (Windows silently drops them), at most 255 chars.
 */
public final class DrivePath {
    private DrivePath() { }

    /** The components, or null when the path is not one this app will touch. */
    public static List<String> split(String path) {
        List<String> out = new ArrayList<>();
        if (path == null) return null;
        if (path.isEmpty()) return out;
        if (path.length() > 1024) return null;
        for (String part : path.split("/", -1)) {
            if (!nameOk(part)) return null;
            out.add(part);
        }
        return out;
    }

    public static boolean nameOk(String n) {
        if (n == null || n.isEmpty() || n.length() > 255 || n.equals(".") || n.equals("..")) return false;
        char last = n.charAt(n.length() - 1);
        if (last == '.' || last == ' ') return false;
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            if (c < 0x20 || c == 0x7f || "\\/:*?\"<>|".indexOf(c) >= 0) return false;
        }
        return true;
    }
}
