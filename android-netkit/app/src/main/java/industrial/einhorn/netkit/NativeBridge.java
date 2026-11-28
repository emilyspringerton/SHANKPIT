package industrial.einhorn.netkit;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Build;
import android.view.MotionEvent;
import android.view.View;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import industrial.einhorn.netkit.shell.ArpTable;
import industrial.einhorn.netkit.shell.Json;
import industrial.einhorn.netkit.shell.NetInfo;
import industrial.einhorn.netkit.shell.Prober;
import industrial.einhorn.netkit.shell.Resolver;
import industrial.einhorn.netkit.shell.Sink;
import industrial.einhorn.netkit.shell.TcpHub;
import industrial.einhorn.netkit.shell.Udp;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * window.NetkitNative -- the whole of what the page needs that a browser sandbox cannot do: raw TCP, probes, the local network, the ARP
 * cache, encrypted storage, the clipboard, a USB drive. Nothing here interprets a byte: SSH, VNC, the scanner's decisions and the terminal are PARENA
 * compiled to WebAssembly (PARENA/stdlib/netkit). The contract is documented where the page uses it:
 * PARENA/stdlib/netkit/host/nk_host_core.prn. Every method returns at once; results come back as window.netkitEvent(json).
 *
 * Only the packaged https://netkit.local origin is ever loaded into the WebView (MainActivity blocks everything else), so this object is
 * reachable only by the page that ships in the APK.
 */
final class NativeBridge implements Sink {
    private final Activity activity;
    private final WebView web;
    private final TcpHub hub;
    private final Prober prober;
    private final Resolver resolver;
    private final Udp udp;
    private final SecureStore store;
    private final DriveStore drive;
    private volatile boolean destroyed;

    NativeBridge(Activity activity, WebView web) {
        this.activity = activity;
        this.web = web;
        this.hub = new TcpHub(this);
        this.prober = new Prober(this);
        this.resolver = new Resolver(this);
        this.udp = new Udp(this);
        this.store = new SecureStore(activity);
        this.drive = new DriveStore(activity, this);
    }

    /** Events from the shell threads -> the page, on the UI thread. */
    @Override public void event(String json) {
        if (destroyed) return;
        final String js = "window.netkitEvent&&window.netkitEvent(" + Json.quote(json) + ")";
        web.post(new Runnable() { @Override public void run() { if (!destroyed) web.evaluateJavascript(js, null); } });
    }

    void shutdown() {
        destroyed = true;
        hub.closeAll();
        prober.shutdown();
        resolver.shutdown();
        udp.shutdown();
        drive.shutdown();
    }

    /** MainActivity hands the folder picker's answer here (null = cancelled). */
    void drivePicked(android.net.Uri uri) { drive.picked(uri); }

    // ---- sockets ---------------------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public void tcpOpen(int id, String host, int port, int timeoutMs) { hub.open(id, host, port, timeoutMs); }
    @JavascriptInterface public void tcpWrite(int id, String base64) { hub.write(id, base64); }
    @JavascriptInterface public void tcpClose(int id) { hub.close(id); }

    // ---- the scanner's probes -----------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public void probe(int job, String ip, int port, int timeoutMs) { prober.probe(job, ip, port, timeoutMs); }
    @JavascriptInterface public void ping(int job, String host, int timeoutMs) { prober.ping(job, host, timeoutMs); }
    @JavascriptInterface public void resolve(String ip) { resolver.resolve(ip); }
    /** One UDP question to one device (the scanner's mDNS / NetBIOS name lookups); the packet is built and parsed in the wasm. */
    @JavascriptInterface public void udpQuery(int id, String host, int port, String base64, int timeoutMs) { udp.query(id, host, port, base64, timeoutMs); }
    @JavascriptInterface public String arpText() { return ArpTable.read(); }
    @JavascriptInterface public String localNets() { return NetInfo.json(); }

    // ---- IDUNA sign-in (GoblinFoxDragon) ---------------------------------------------------------------------------------------------------
    /** Opens IDUNA's sign-in page in the system browser (the URL is fixed in SsoLink); the answer comes back as a {t:"sso"} event. */
    @JavascriptInterface public void ssoOpen() {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    // a Custom Tab (Chrome's in-app tab, asked for with the bare extra: no androidx dependency): it opens in this task,
                    // so when sso.html hands shankphone://sso back, the singleTask activity clears it and you are back in the app.
                    android.content.Intent i = new android.content.Intent(android.content.Intent.ACTION_VIEW,
                            android.net.Uri.parse(industrial.einhorn.netkit.shell.SsoLink.SIGN_IN_URL));
                    android.os.Bundle extras = new android.os.Bundle();
                    extras.putBinder("android.support.customtabs.extra.SESSION", null);
                    i.putExtras(extras);
                    i.putExtra("android.support.customtabs.extra.TOOLBAR_COLOR", 0xFF0E1319);
                    i.putExtra("android.support.customtabs.extra.TITLE_VISIBILITY", 1);
                    activity.startActivity(i);
                } catch (android.content.ActivityNotFoundException e) {
                    event(Json.obj("t", "sso", "e", "nobrowser"));
                }
            }
        });
    }
    /** MainActivity hands a shankphone://sso link here: kept (encrypted) for the next launch, and told to the page. */
    void ssoReturned(String token, String player) {
        store.set("gfd_sso", Json.obj("token", token, "player", player));
        event(Json.obj("t", "sso", "e", "ok", "player", player));
    }
    @JavascriptInterface public void ssoForget() { store.delete("gfd_sso"); }

    // ---- storage ---------------------------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public String storeGet(String key) { return store.get(key); }
    @JavascriptInterface public void storeSet(String key, String value) { store.set(key, value); }
    @JavascriptInterface public void storeDel(String key) { store.delete(key); }

    // ---- the USB drive (Android's folder picker + storage framework; paths are "" or "folder/name") -------------------------------------
    @JavascriptInterface public String driveState() { return drive.state(); }
    @JavascriptInterface public void driveOpen() { drive.open(); }
    @JavascriptInterface public void driveForget() { drive.forget(); }
    @JavascriptInterface public void driveList(String path) { drive.list(path); }
    @JavascriptInterface public void driveRead(String path) { drive.read(path); }
    @JavascriptInterface public void driveWrite(String path, String base64) { drive.write(path, base64); }

    // ---- FPS-style mouse (the page asks for it while a first-person world is showing) -----------------------------------------------------------
    // WebView has no Pointer Lock, so the app captures the pointer itself (API 26+): the mouse then stops moving a cursor and reports raw
    // relative motion, buttons and the wheel, which go to window.netkitMouse as {"dx","dy","b","w"}, coalesced to one call per frame.
    // Keyboard keys need nothing: a focused WebView already turns them into DOM key events (WASD, Space, 1-0, Tab ... all forwarded by the page).
    private float mdx, mdy;
    private int mbtn, mwheel;
    private boolean mflushPending;
    private final Runnable mflush = new Runnable() {
        @Override public void run() {
            mflushPending = false;
            if (destroyed) return;
            String js = "window.netkitMouse&&window.netkitMouse(" + Json.quote(
                    "{\"dx\":" + mdx + ",\"dy\":" + mdy + ",\"b\":" + mbtn + ",\"w\":" + mwheel + "}") + ")";
            mdx = 0f; mdy = 0f; mwheel = 0;
            web.evaluateJavascript(js, null);
        }
    };

    @JavascriptInterface public void mouseCapture(final int on) {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (Build.VERSION.SDK_INT < 26 || destroyed) return;
                capture(on != 0);
            }
        });
    }

    @android.annotation.TargetApi(26)
    private void capture(boolean on) {
        if (on) {
            web.setFocusable(true);
            web.setFocusableInTouchMode(true);
            web.requestFocus();
            web.setOnCapturedPointerListener(new View.OnCapturedPointerListener() {
                @Override public boolean onCapturedPointer(View v, MotionEvent e) { return capturedMouse(e); }
            });
            web.requestPointerCapture();
        } else {
            web.releasePointerCapture();
            web.setOnCapturedPointerListener(null);
            mdx = 0f; mdy = 0f; mwheel = 0; mbtn = 0;
        }
    }

    private boolean capturedMouse(MotionEvent e) {
        if (e.getActionMasked() == MotionEvent.ACTION_SCROLL) {
            float v = e.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (v != 0f) mwheel = v > 0f ? 1 : -1;
        } else {
            mdx += e.getX();
            mdy += e.getY();
        }
        mbtn = e.getButtonState() & 3;     // BUTTON_PRIMARY | BUTTON_SECONDARY
        if (!mflushPending) { mflushPending = true; web.post(mflush); }
        return true;
    }

    // ---- clipboard (UI thread) --------------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public void clipboardSet(final String text) {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                ClipboardManager cm = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
                if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("Netkit", text));
            }
        });
    }

    @JavascriptInterface public String clipboardGet() {
        final AtomicReference<String> out = new AtomicReference<>("");
        final CountDownLatch done = new CountDownLatch(1);
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    ClipboardManager cm = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
                    ClipData d = cm == null ? null : cm.getPrimaryClip();
                    if (d != null && d.getItemCount() > 0 && d.getItemAt(0).getText() != null) out.set(d.getItemAt(0).getText().toString());
                } finally { done.countDown(); }
            }
        });
        try { done.await(1500, TimeUnit.MILLISECONDS); } catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        return out.get();
    }
}
