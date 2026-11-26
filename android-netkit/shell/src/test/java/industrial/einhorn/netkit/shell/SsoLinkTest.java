package industrial.einhorn.netkit.shell;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class SsoLinkTest {
    private static final String T = "eyJhbGciOiJFUzI1NiJ9.eyJzdWIiOiJsb2NhbDo0MiJ9.c2lnbmF0dXJlX2J5dGVz";

    @Test public void readsTheTokenAndPlayer() {
        assertArrayEquals(new String[] { T, "42" }, SsoLink.parse("shankphone://sso#sso_token=" + T + "&player_id=42"));
    }
    @Test public void playerIsOptional() {
        assertArrayEquals(new String[] { T, "" }, SsoLink.parse("shankphone://sso#sso_token=" + T));
    }
    @Test public void refusesOtherLinksAndBadTokens() {
        assertNull(SsoLink.parse("https://evil.example/#sso_token=" + T));
        assertNull(SsoLink.parse("shankphone://sso"));
        assertNull(SsoLink.parse("shankphone://sso#sso_token=not-a-jwt"));
        assertNull(SsoLink.parse("shankphone://sso#sso_token=a.b.c\"><script>"));
        assertNull(SsoLink.parse(null));
    }
    @Test public void aStrangePlayerIdIsDropped() {
        assertArrayEquals(new String[] { T, "" }, SsoLink.parse("shankphone://sso#sso_token=" + T + "&player_id=<x>"));
    }
    @Test public void theSignInUrlIsFixedToIduna() {
        assertTrue(SsoLink.SIGN_IN_URL.startsWith("https://iam.okemily.com/api/v1/auth/sso/login?redirect_uri=https%3A%2F%2Fwotan.okemily.com%2F"));
    }
}
