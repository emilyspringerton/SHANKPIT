package industrial.einhorn.netkit;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import industrial.einhorn.netkit.shell.ArpTable;
import industrial.einhorn.netkit.shell.Json;
import industrial.einhorn.netkit.shell.NetInfo;
import industrial.einhorn.netkit.shell.Prober;
import industrial.einhorn.netkit.shell.Resolver;
import industrial.einhorn.netkit.shell.Sink;
import industrial.einhorn.netkit.shell.TcpHub;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * window.NetkitNative -- the whole of what the page needs that a browser sandbox cannot do: raw TCP, probes, the local network, the ARP
 * cache, encrypted storage, the clipboard. Nothing here interprets a byte: SSH, VNC, the scanner's decisions and the terminal are PARENA
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
    private final SecureStore store;
    private volatile boolean destroyed;

    NativeBridge(Activity activity, WebView web) {
        this.activity = activity;
        this.web = web;
        this.hub = new TcpHub(this);
        this.prober = new Prober(this);
        this.resolver = new Resolver(this);
        this.store = new SecureStore(activity);
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
    }

    // ---- sockets ---------------------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public void tcpOpen(int id, String host, int port, int timeoutMs) { hub.open(id, host, port, timeoutMs); }
    @JavascriptInterface public void tcpWrite(int id, String base64) { hub.write(id, base64); }
    @JavascriptInterface public void tcpClose(int id) { hub.close(id); }

    // ---- the scanner's probes -----------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public void probe(int job, String ip, int port, int timeoutMs) { prober.probe(job, ip, port, timeoutMs); }
    @JavascriptInterface public void ping(int job, String host, int timeoutMs) { prober.ping(job, host, timeoutMs); }
    @JavascriptInterface public void resolve(String ip) { resolver.resolve(ip); }
    @JavascriptInterface public String arpText() { return ArpTable.read(); }
    @JavascriptInterface public String localNets() { return NetInfo.json(); }

    // ---- storage ---------------------------------------------------------------------------------------------------------------------------------
    @JavascriptInterface public String storeGet(String key) { return store.get(key); }
    @JavascriptInterface public void storeSet(String key, String value) { store.set(key, value); }
    @JavascriptInterface public void storeDel(String key) { store.delete(key); }

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
