package industrial.einhorn.netkit;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.webkit.ValueCallback;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import industrial.einhorn.shankpit.netkit.BuildConfig;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;

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
        setContentView(web);

        Log.i(TAG, "loading " + ORIGIN + "/index.html");
        web.loadUrl(ORIGIN + "/index.html");
    }

    /** The page decides what Back does (close a dialog, close a session); if it does nothing, the app finishes. */
    @Override
    public void onBackPressed() {
        web.evaluateJavascript("(window.netkitBack && window.netkitBack())", new ValueCallback<String>() {
            @Override public void onReceiveValue(String handled) { if (!"true".equals(handled)) MainActivity.super.onBackPressed(); }
        });
    }

    @Override protected void onPause() { if (web != null) web.onPause(); super.onPause(); }
    @Override protected void onResume() { super.onResume(); if (web != null) web.onResume(); }

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
                        "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; form-action 'none'");
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
            String p = path.toLowerCase();
            if (p.endsWith(".html")) return "text/html";
            if (p.endsWith(".js") || p.endsWith(".mjs")) return "text/javascript";
            if (p.endsWith(".wasm")) return "application/wasm";
            if (p.endsWith(".css")) return "text/css";
            if (p.endsWith(".json")) return "application/json";
            if (p.endsWith(".png")) return "image/png";
            if (p.endsWith(".svg")) return "image/svg+xml";
            return "application/octet-stream";
        }
    }
}
