package industrial.einhorn.netkit.core;

/** Why a connection failed -- the four user-visible reasons VS0 promises, plus fallbacks. */
public enum ErrorKind {
    UNREACHABLE,   // no route / host down / DNS failure
    PORT_CLOSED,   // host answered but nothing listens (connection refused)
    TIMEOUT,       // host asleep or filtered: no answer in time
    AUTH_FAILED,   // wrong password / username
    PROTOCOL,      // server spoke something we do not understand
    UNKNOWN
}
