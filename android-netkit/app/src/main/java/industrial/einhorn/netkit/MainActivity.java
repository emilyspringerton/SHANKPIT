package industrial.einhorn.netkit;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.webkit.ValueCallback;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.window.OnBackInvokedDispatcher;

import industrial.einhorn.shankpit.netkit.BuildConfig;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Locale;

/**
 * SHANKPIT Netkit's only Activity: a WebView that runs the page from the APK's own assets (index.html + nk-host.js + three .wasm files,
 * all generated from PARENA) with NetkitNative as its window to the network. Same virtual-origin pattern as the shankpit-460 app next
 * door, so fetch() and WebAssembly.compile() behave exactly as in a browser. Nothing else is ever loaded: other origins are blocked.
 */
public class MainActivity extends Activity {
    private static final String TAG = "netkit";
    private static final String ORIGIN = "https://netkit.local";

    private WebView web;
    private NativeBridge bridge;

    // JavaScript is the product: the page's logic is PARENA compiled to wasm + generated JS, and only the packaged origin is ever loaded.
    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        web = new WebView(this);
        web.setBackgroundColor(0xFF0E1319);
        if (BuildConfig.DEBUG) WebView.setWebContentsDebuggingEnabled(true);

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);

        bridge = new NativeBridge(this, web);
        web.addJavascriptInterface(bridge, "NetkitNative");
        web.setWebViewClient(new BundleClient());
        web.setFocusable(true);
        web.setFocusableInTouchMode(true);
        setContentView(web);
        web.requestFocus();      // a hardware keyboard / mouse goes straight to the page

        Log.i(TAG, "loading " + ORIGIN + "/index.html");
        web.loadUrl(ORIGIN + "/index.html");
        handleSsoLink(getIntent());

        // Android 13+: predictive back (enableOnBackInvokedCallback in the manifest); older releases call onBackPressed below.
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::askPageBack);
        }
    }

    /** A shankphone://sso#sso_token=... link (IDUNA sign-in coming back through wotan.okemily.com/shankphone/sso.html). */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleSsoLink(intent);
    }

    private void handleSsoLink(Intent intent) {
        if (intent == null || intent.getData() == null || bridge == null) return;
        String[] tp = industrial.einhorn.netkit.shell.SsoLink.parse(intent.getData().toString());
        if (tp != null) bridge.ssoReturned(tp[0], tp[1]);
    }

    /** The page decides what Back does (close a dialog, close a session); if it does nothing, the app finishes. */
    private void askPageBack() {
        if (web == null) { finish(); return; }
        web.evaluateJavascript("(window.netkitBack && window.netkitBack())", new ValueCallback<String>() {
            @Override public void onReceiveValue(String handled) { if (!"true".equals(handled)) finish(); }
        });
    }

    /** Android 12 and older. */
    @Deprecated
    @Override
    public void onBackPressed() { askPageBack(); }

    /** The folder picker DriveStore opened (the USB drive the editor saves to). */
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == DriveStore.PICK && bridge != null) bridge.drivePicked(resultCode == RESULT_OK && data != null ? data.getData() : null);
    }

    @Override protected void onPause() { if (web != null) web.onPause(); super.onPause(); }
    @Override protected void onResume() { super.onResume(); if (web != null) { web.onResume(); web.requestFocus(); } }

    @Override
    protected void onDestroy() {
        if (bridge != null) { bridge.shutdown(); bridge = null; }
        if (web != null) { web.destroy(); web = null; }
        super.onDestroy();
    }

    /** Serves the packaged page from the virtual origin; nothing is ever fetched from the network by the WebView itself. */
    private static final class BundleClient extends WebViewClient {
        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            return true;      // the page never navigates; a link to anywhere else is ignored
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            Uri url = request.getUrl();
            if (url == null || !ORIGIN.equals(url.getScheme() + "://" + url.getHost())) return status(403, "Forbidden");
            String path = url.getPath();
            if (path == null || path.isEmpty() || "/".equals(path)) path = "/index.html";
            path = path.substring(1);
            if (path.contains("..") || path.endsWith(".ts") || path.endsWith(".md")) return status(404, "Not Found");
            try {
                InputStream in = view.getContext().getAssets().open(path);
                String type = mimeFor(path);
                HashMap<String, String> headers = new HashMap<>();
                headers.put("Cache-Control", "no-store");
                headers.put("Content-Security-Policy",
                        "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self' wss://wotan.okemily.com; base-uri 'none'; form-action 'none'");
                return new WebResourceResponse(type, type.startsWith("text/") ? "utf-8" : null, 200, "OK", headers, in);
            } catch (IOException e) {
                Log.w(TAG, "missing asset: " + path);
                return status(404, "Not Found");
            }
        }

        private static WebResourceResponse status(int code, String text) {
            return new WebResourceResponse("text/plain", "utf-8", code, text, new HashMap<String, String>(),
                    new ByteArrayInputStream(text.getBytes()));
        }

        private static String mimeFor(String path) {
            String p = path.toLowerCase(Locale.ROOT);
            if (p.endsWith(".html")) return "text/html";
            if (p.endsWith(".js") || p.endsWith(".mjs")) return "text/javascript";
            if (p.endsWith(".wasm")) return "application/wasm";
            if (p.endsWith(".css")) return "text/css";
            if (p.endsWith(".json")) return "application/json";
            if (p.endsWith(".png")) return "image/png";
            if (p.endsWith(".svg")) return "image/svg+xml";
            if (p.endsWith(".bat") || p.endsWith(".txt") || p.endsWith(".example")) return "text/plain";
            return "application/octet-stream";
        }
    }
}
