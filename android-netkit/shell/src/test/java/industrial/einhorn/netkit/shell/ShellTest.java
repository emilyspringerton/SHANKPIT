package industrial.einhorn.netkit.shell;

import static org.junit.Assert.*;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.Test;

/** The shell moves bytes and classifies connects; these tests run against real loopback sockets. */
public class ShellTest {

    // ---- B64 / Json --------------------------------------------------------------------------------------------------------------------
    @Test public void base64MatchesTheJdkForEveryLength() {
        Random r = new Random(1);
        for (int n = 0; n < 300; n++) {
            byte[] b = new byte[n]; r.nextBytes(b);
            String want = java.util.Base64.getEncoder().encodeToString(b);
            assertEquals(want, B64.encode(b, 0, n));
            assertArrayEquals(b, B64.decode(want));
        }
        byte[] big = new byte[1_000_000]; r.nextBytes(big);
        assertArrayEquals(big, B64.decode(B64.encode(big, 0, big.length)));
        assertEquals(java.util.Base64.getEncoder().encodeToString(new byte[] {1, 2, 3, 4, 5}), B64.encode(new byte[] {9, 1, 2, 3, 4, 5, 9}, 1, 5));
    }

    @Test(expected = IllegalArgumentException.class) public void base64RejectsGarbage() { B64.decode("ab*d"); }

    @Test public void jsonEscapesWhatTheNetworkCanSendUs() {
        assertEquals("\"a\\\"b\\\\c\\n\\u0001\"", Json.quote("a\"b\\c\n\u0001"));
        assertEquals("\"\\u2028\\u2029\"", Json.quote("\u2028\u2029"));
        String hostile = "</script><img src=x onerror=alert(1)>\u0000";
        assertFalse("a control character must never reach the page raw", Json.quote(hostile).contains("\u0000"));
        assertEquals("{\"t\":\"tcp\",\"id\":7,\"e\":\"open\"}", Json.obj("t", "tcp", "id", 7, "e", "open"));
    }

    // ---- Codes / Prober -----------------------------------------------------------------------------------------------------------------
    @Test public void proberClassifiesOpenAndRefused() throws Exception {
        try (ServerSocket ss = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"))) {
            assertEquals(Codes.OPEN, Prober.attempt("127.0.0.1", ss.getLocalPort(), 2000));
        }
        int closed;
        try (ServerSocket ss = new ServerSocket(0)) { closed = ss.getLocalPort(); }
        assertEquals(Codes.REFUSED, Prober.attempt("127.0.0.1", closed, 2000));
        assertEquals(Codes.UNREACHABLE, Prober.attempt("no-such-host.invalid", 22, 2000));
    }

    @Test public void proberReportsEventsWithJobNumbersAndTimes() throws Exception {
        Events ev = new Events(); Prober p = new Prober(ev);
        try (ServerSocket ss = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"))) {
            p.probe(41, "127.0.0.1", ss.getLocalPort(), 2000);
            String e = ev.await(3000, "\"t\":\"probe\"", "\"job\":41", "\"code\":0");
            assertNotNull(e);
            assertTrue(e.matches(".*\"ms\":\\d+.*"));
        }
        int closed; try (ServerSocket ss = new ServerSocket(0)) { closed = ss.getLocalPort(); }
        p.probe(42, "127.0.0.1", closed, 2000);
        assertNotNull(ev.await(3000, "\"job\":42", "\"code\":1"));
        p.ping(1000001, "127.0.0.1", 2000);
        assertNotNull("loopback is up: ping answers 0", ev.await(5000, "\"job\":1000001", "\"code\":0"));
        p.shutdown();
    }

    @Test public void manyProbesAtOnce() throws Exception {
        Events ev = new Events(); Prober p = new Prober(ev);
        int closed; try (ServerSocket ss = new ServerSocket(0)) { closed = ss.getLocalPort(); }
        for (int j = 0; j < 500; j++) p.probe(j, "127.0.0.1", closed, 2000);
        long end = System.currentTimeMillis() + 15000;
        while (ev.copy().size() < 500 && System.currentTimeMillis() < end) Thread.sleep(20);
        assertEquals("every probe is answered exactly once", 500, ev.copy().size());
        java.util.Set<String> jobs = new java.util.HashSet<>();
        Matcher m = Pattern.compile("\"job\":(\\d+)").matcher("");
        for (String e : ev.copy()) { m.reset(e); assertTrue(m.find()); jobs.add(m.group(1)); }
        assertEquals(500, jobs.size());
        p.shutdown();
    }

    // ---- TcpHub ---------------------------------------------------------------------------------------------------------------------------
    private static byte[] payload(String json) {
        Matcher m = Pattern.compile("\"b\":\"([^\"]*)\"").matcher(json);
        assertTrue(json, m.find()); return B64.decode(m.group(1));
    }

    @Test public void tcpEchoRoundTripInOrder() throws Exception {
        try (final ServerSocket ss = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"))) {
            Thread echo = new Thread(() -> {
                try (Socket c = ss.accept()) { InputStream in = c.getInputStream(); OutputStream out = c.getOutputStream(); byte[] b = new byte[4096]; int n; while ((n = in.read(b)) > 0) { out.write(b, 0, n); out.flush(); } } catch (Exception ignored) { }
            });
            echo.setDaemon(true); echo.start();
            Events ev = new Events(); TcpHub hub = new TcpHub(ev);
            hub.open(5, "127.0.0.1", ss.getLocalPort(), 3000);
            // written BEFORE the connect finishes: must still go out, in order
            hub.write(5, B64.encode("hello ".getBytes(StandardCharsets.UTF_8), 0, 6));
            hub.write(5, B64.encode("world".getBytes(StandardCharsets.UTF_8), 0, 5));
            assertNotNull(ev.await(3000, "\"id\":5", "\"e\":\"open\""));
            ByteArrayOutputStream got = new ByteArrayOutputStream();
            long end = System.currentTimeMillis() + 5000;
            while (got.size() < 11 && System.currentTimeMillis() < end) {
                Thread.sleep(10);
                got.reset();
                for (String e : ev.copy()) if (e.contains("\"e\":\"data\"") && e.contains("\"id\":5")) got.write(payload(e));
            }
            assertEquals("hello world", got.toString("UTF-8"));
            hub.close(5);
            assertEquals(0, hub.openCount());
            hub.closeAll();
        }
    }

    @Test public void tcpMovesAMegabyteIntact() throws Exception {
        final byte[] data = new byte[1_000_000]; new Random(9).nextBytes(data);
        try (final ServerSocket ss = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"))) {
            Thread srv = new Thread(() -> { try (Socket c = ss.accept()) { c.getOutputStream().write(data); c.getOutputStream().flush(); Thread.sleep(300); } catch (Exception ignored) { } });
            srv.setDaemon(true); srv.start();
            Events ev = new Events(); TcpHub hub = new TcpHub(ev);
            hub.open(6, "127.0.0.1", ss.getLocalPort(), 3000);
            assertNotNull(ev.await(10000, "\"id\":6", "\"e\":\"closed\""));
            ByteArrayOutputStream got = new ByteArrayOutputStream();
            for (String e : ev.copy()) if (e.contains("\"e\":\"data\"")) got.write(payload(e));
            assertArrayEquals("a megabyte arrives complete and in order, then 'closed' is reported last", data, got.toByteArray());
            String last = ev.copy().get(ev.copy().size() - 1);
            assertTrue(last.contains("\"e\":\"closed\""));
            hub.closeAll();
        }
    }

    @Test public void tcpRefusedAndUnknownHostAreCoded() throws Exception {
        int closed; try (ServerSocket ss = new ServerSocket(0)) { closed = ss.getLocalPort(); }
        Events ev = new Events(); TcpHub hub = new TcpHub(ev);
        hub.open(7, "127.0.0.1", closed, 2000);
        assertNotNull(ev.await(3000, "\"id\":7", "\"e\":\"error\"", "\"code\":1"));
        hub.open(8, "no-such-host.invalid", 22, 2000);
        assertNotNull(ev.await(5000, "\"id\":8", "\"e\":\"error\"", "\"code\":3"));
        hub.open(9, "127.0.0.1", 70000, 2000);
        assertNotNull("an impossible port is an error event, not a crashed thread", ev.await(3000, "\"id\":9", "\"e\":\"error\""));
        assertEquals(0, hub.openCount());
        hub.closeAll();
    }

    @Test public void tcpSlowConnectTimesOutOrIsUnreachable() throws Exception {
        Events ev = new Events(); TcpHub hub = new TcpHub(ev);
        hub.open(10, "10.255.255.1", 22, 600);       // nobody answers here: a timeout (or "network unreachable" on a box with no route)
        String e = ev.await(4000, "\"id\":10", "\"e\":\"error\"");
        assertNotNull(e);
        assertTrue(e, e.contains("\"code\":2") || e.contains("\"code\":3") || e.contains("\"code\":4"));
        hub.closeAll();
    }

    @Test public void closingWhileConnectingNeverReportsOpen() throws Exception {
        Events ev = new Events(); TcpHub hub = new TcpHub(ev);
        hub.open(11, "10.255.255.1", 22, 3000);
        hub.close(11);
        Thread.sleep(500);
        for (String e : ev.copy()) assertFalse(e, e.contains("\"e\":\"open\""));
        hub.closeAll();
    }

    @Test public void theServerHangingUpIsReported() throws Exception {
        try (final ServerSocket ss = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"))) {
            Thread srv = new Thread(() -> { try (Socket c = ss.accept()) { c.getOutputStream().write("bye".getBytes()); } catch (Exception ignored) { } });
            srv.setDaemon(true); srv.start();
            Events ev = new Events(); TcpHub hub = new TcpHub(ev);
            hub.open(12, "127.0.0.1", ss.getLocalPort(), 3000);
            assertNotNull(ev.await(3000, "\"id\":12", "\"e\":\"closed\""));
            assertEquals(0, hub.openCount());
            hub.closeAll();
        }
    }

    // ---- NetInfo / ArpTable / Resolver ------------------------------------------------------------------------------------------------------
    @Test public void cidrMath() {
        assertEquals("192.168.4.0/24", NetInfo.cidr(new byte[] {(byte) 192, (byte) 168, 4, 23}, 24));
        assertEquals("10.0.0.0/8", NetInfo.cidr(new byte[] {10, 9, 8, 7}, 8));
        assertEquals("172.16.0.0/12", NetInfo.cidr(new byte[] {(byte) 172, 31, (byte) 255, 1}, 12));
        assertEquals("192.168.4.20/30", NetInfo.cidr(new byte[] {(byte) 192, (byte) 168, 4, 22}, 30));
        assertEquals("1.2.3.4/32", NetInfo.cidr(new byte[] {1, 2, 3, 4}, 32));
        assertEquals("200.1.1.0/24", NetInfo.cidr(new byte[] {(byte) 200, 1, 1, (byte) 255}, 24));
        assertTrue(NetInfo.cellular("rmnet_data0")); assertTrue(NetInfo.cellular("ccmni1")); assertFalse(NetInfo.cellular("wlan0")); assertFalse(NetInfo.cellular("eth0"));
    }

    @Test public void netInfoIsWellFormedJson() {
        String j = NetInfo.json();
        assertTrue(j, j.matches("\\{\"cidrs\":\\[(\"[0-9./]+\"(,\"[0-9./]+\")*)?\\],\"ips\":\\[(\"[0-9.]+\"(,\"[0-9.]+\")*)?\\]\\}"));
    }

    @Test public void arpTableReadsAFileAndSurvivesAMissingOne() throws Exception {
        File f = File.createTempFile("arp", ".txt"); f.deleteOnExit();
        Files.write(f.toPath(), "IP address       HW type     Flags       HW address            Mask     Device\n192.168.4.1 0x1 0x2 aa:bb:cc:dd:ee:ff * wlan0\n".getBytes(StandardCharsets.UTF_8));
        assertTrue(ArpTable.read(f.getPath()).contains("aa:bb:cc:dd:ee:ff"));
        assertEquals("", ArpTable.read("/no/such/file"));
    }

    @Test public void resolverNamesLoopback() throws Exception {
        Events ev = new Events(); Resolver r = new Resolver(ev);
        r.resolve("127.0.0.1");
        String e = ev.await(5000, "\"t\":\"name\"", "\"ip\":\"127.0.0.1\"");
        assertNotNull(e);
        r.shutdown();
    }
}
