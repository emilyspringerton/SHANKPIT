package industrial.einhorn.netkit.shell;

/**
 * The SHANKPHONE's IDUNA sign-in, the half that comes back. The app opens IDUNA's own sign-in page in the system browser
 * ({@link #SIGN_IN_URL}); after signing in, IDUNA redirects to wotan.okemily.com/shankphone/sso.html with
 * "#sso_token=...&player_id=..." and that page forwards the same fragment to "shankphone://sso#...", which Android hands to the app.
 * This class only reads that link. It never trusts it for anything but "a token to try": the MUD and IDUNA verify the token itself.
 */
public final class SsoLink {
    private SsoLink() {}

    /** Fixed: the page cannot choose where the browser goes. */
    public static final String SIGN_IN_URL =
            "https://iam.okemily.com/api/v1/auth/sso/login?redirect_uri=https%3A%2F%2Fwotan.okemily.com%2Fshankphone%2Fsso.html";

    /** The token and the player id, or null when the link is not a well-formed sign-in return. */
    public static String[] parse(String uri) {
        if (uri == null || !uri.startsWith("shankphone://sso")) return null;
        int hash = uri.indexOf('#');
        if (hash < 0) return null;
        String token = null, player = "";
        for (String part : uri.substring(hash + 1).split("&")) {
            int eq = part.indexOf('=');
            if (eq <= 0) continue;
            String k = part.substring(0, eq), v = part.substring(eq + 1);
            if (k.equals("sso_token")) token = v;
            else if (k.equals("player_id")) player = v;
        }
        // a JWT is three base64url parts; anything else is not passed on
        if (token == null || token.length() < 20 || token.length() > 8192 || !token.matches("[A-Za-z0-9_\\-]+\\.[A-Za-z0-9_\\-]+\\.[A-Za-z0-9_\\-]+")) return null;
        if (!player.matches("[A-Za-z0-9:_\\-]{0,128}")) player = "";
        return new String[] { token, player };
    }
}
