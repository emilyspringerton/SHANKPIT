package industrial.einhorn.shankpit;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;

/**
 * shankpit-460 on Android: the game is the PARENA-compiled WebAssembly build
 * already shipped on WOTAN, running in a WebView. There is no Android port of
 * the C engine here -- the wasm module has no imports and renders into its own
 * 640x360 framebuffer, so the whole platform layer is this file plus the touch
 * controls in apps/shankpit-460/android-controls.js.
 *
 * The bundle is served from a virtual HTTPS origin rather than file:// so that
 * fetch() and WebAssembly.instantiate() behave exactly as they do in a browser.
 * Nothing is ever loaded from the network except matchmaking (and only when a
 * backend is configured).
 */
public class MainActivity extends Activity {

    private static final String TAG = "shankpit460";
    private static final String ORIGIN = "https://shankpit.local";
    private static final String ASSET_ROOT = "";

    private WebView web;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemUi();

        web = new WebView(this);
        web.setBackgroundColor(0xFF11161C);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setVerticalScrollBarEnabled(false);
        web.setHorizontalScrollBarEnabled(false);
        if (BuildConfig.DEBUG) {
            WebView.setWebContentsDebuggingEnabled(true);
        }

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        // The first tap starts the match and is also the gesture that unlocks audio.
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setUseWideViewPort(false);
        s.setLoadWithOverviewMode(false);

        web.setWebChromeClient(new WebChromeClient());
        web.setWebViewClient(new BundleClient());

        setContentView(web);

        String backend = BuildConfig.SHANKPIT_BACKEND == null ? "" : BuildConfig.SHANKPIT_BACKEND.trim();
        String url = ORIGIN + "/index.html";
        if (!backend.isEmpty()) {
            url += "?backend=" + Uri.encode(backend);
        }
        Log.i(TAG, "loading " + url + (backend.isEmpty() ? " (offline: bot demo)" : ""));
        web.loadUrl(url);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemUi() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    @Override
    protected void onPause() {
        if (web != null) web.onPause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (web != null) web.onResume();
    }

    @Override
    protected void onDestroy() {
        if (web != null) {
            web.destroy();
            web = null;
        }
        super.onDestroy();
    }

    /** Serves the packaged game from the virtual origin; never touches the network. */
    private static final class BundleClient extends WebViewClient {

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            Uri url = request.getUrl();
            if (url == null || !ORIGIN.equals(url.getScheme() + "://" + url.getHost())) {
                return null; // let matchmaking calls go to the real backend, if any
            }
            String path = url.getPath();
            if (path == null || path.isEmpty() || "/".equals(path)) {
                path = "/index.html";
            }
            path = path.substring(1);
            if (path.contains("..")) {
                return notFound();
            }
            try {
                InputStream in = view.getContext().getAssets().open(ASSET_ROOT + path);
                String type = mimeFor(path);
                String encoding = type.startsWith("text/") ? "utf-8" : null;
                HashMap<String, String> headers = new HashMap<>();
                headers.put("Cache-Control", "no-store");
                return new WebResourceResponse(type, encoding, 200, "OK", headers, in);
            } catch (IOException e) {
                Log.w(TAG, "missing asset: " + path);
                return notFound();
            }
        }

        private static WebResourceResponse notFound() {
            HashMap<String, String> headers = new HashMap<>();
            return new WebResourceResponse("text/plain", "utf-8", 404, "Not Found", headers,
                    new ByteArrayInputStream("not found".getBytes()));
        }

        private static String mimeFor(String path) {
            String p = path.toLowerCase();
            if (p.endsWith(".html")) return "text/html";
            if (p.endsWith(".js") || p.endsWith(".mjs")) return "text/javascript";
            if (p.endsWith(".wasm")) return "application/wasm";
            if (p.endsWith(".css")) return "text/css";
            if (p.endsWith(".json")) return "application/json";
            if (p.endsWith(".png")) return "image/png";
            if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
            if (p.endsWith(".svg")) return "image/svg+xml";
            if (p.endsWith(".webp")) return "image/webp";
            if (p.endsWith(".ogg")) return "audio/ogg";
            if (p.endsWith(".mp3")) return "audio/mpeg";
            return "application/octet-stream";
        }
    }
}
