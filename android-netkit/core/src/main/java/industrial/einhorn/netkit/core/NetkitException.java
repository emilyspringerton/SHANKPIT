package industrial.einhorn.netkit.core;

public class NetkitException extends Exception {
    public final ErrorKind kind;
    public NetkitException(ErrorKind kind, String message) { super(message); this.kind = kind; }
    public NetkitException(ErrorKind kind, String message, Throwable cause) { super(message, cause); this.kind = kind; }
}
