package industrial.einhorn.netkit;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Log;

import industrial.einhorn.netkit.shell.B64;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/**
 * The page's key/value store (saved connections, pinned devices, trusted host-key fingerprints, remembered passwords).
 * Values are AES-256-GCM encrypted with a key that lives in the Android Keystore and never leaves it, so the stored file is useless off
 * this device. If the Keystore is unavailable the value is stored WITHOUT encryption (marked "p0:") rather than lost -- the app-private
 * directory still protects it from other apps -- and encrypted() says which happened.
 */
final class SecureStore {
    private static final String TAG = "netkit";
    private static final String ALIAS = "netkit-store-v1";
    private final SharedPreferences prefs;
    private SecretKey key;
    private boolean keystoreOk = true;

    SecureStore(Context ctx) {
        prefs = ctx.getSharedPreferences("netkit-store", Context.MODE_PRIVATE);
        try { key = loadOrCreateKey(); } catch (Exception e) { keystoreOk = false; Log.w(TAG, "Keystore unavailable; values will not be encrypted at rest: " + e); }
    }

    boolean encrypted() { return keystoreOk; }

    private static SecretKey loadOrCreateKey() throws Exception {
        KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
        ks.load(null);
        if (ks.containsAlias(ALIAS)) return (SecretKey) ks.getKey(ALIAS, null);
        KeyGenerator kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        kg.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return kg.generateKey();
    }

    synchronized String get(String k) {
        String v = prefs.getString(k, null);
        if (v == null) return "";
        try {
            if (v.startsWith("p0:")) return v.substring(3);
            if (v.startsWith("v1:") && keystoreOk) {
                String[] p = v.split(":", 3);
                Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
                c.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(128, B64.decode(p[1])));
                return new String(c.doFinal(B64.decode(p[2])), StandardCharsets.UTF_8);
            }
        } catch (Exception e) {
            Log.w(TAG, "could not read stored value " + k + ": " + e);
        }
        return "";
    }

    synchronized void set(String k, String value) {
        String stored;
        try {
            if (!keystoreOk) throw new IllegalStateException("no keystore");
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.ENCRYPT_MODE, key);
            byte[] iv = c.getIV();
            byte[] ct = c.doFinal(value.getBytes(StandardCharsets.UTF_8));
            stored = "v1:" + B64.encode(iv, 0, iv.length) + ":" + B64.encode(ct, 0, ct.length);
        } catch (Exception e) {
            stored = "p0:" + value;
        }
        prefs.edit().putString(k, stored).apply();
    }

    synchronized void delete(String k) { prefs.edit().remove(k).apply(); }
}
