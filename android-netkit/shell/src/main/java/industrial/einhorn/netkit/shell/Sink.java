package industrial.einhorn.netkit.shell;

/** Where the shell's events go: one JSON object per call, delivered to the page as window.netkitEvent(json). */
public interface Sink {
    void event(String json);
}
