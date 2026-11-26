package industrial.einhorn.netkit;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import industrial.einhorn.netkit.shell.B64;
import industrial.einhorn.netkit.shell.DrivePath;
import industrial.einhorn.netkit.shell.Json;
import industrial.einhorn.netkit.shell.Sink;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The USB drive (or SD card, or any folder) the user picked in Android's own folder picker. Android's storage framework is the only way an
 * app can write to a USB stick plugged into a tablet: the user chooses the drive once, the app keeps that grant, and every read and write
 * goes through it. This class only moves bytes; what to write (the editor's document, the VNC kit) is decided by the page, in PARENA.
 *
 * Events: {t:"drive", e:"open"|"cancel"|"list"|"read"|"wrote"|"error", ...}. Everything off the UI thread, one operation at a time.
 */
final class DriveStore {
    static final int PICK = 4101;
    private static final String PREFS = "netkit_drive";
    private static final String KEY = "tree";
    private static final int MAX_READ = 4 * 1024 * 1024;

    private final Activity activity;
    private final Sink sink;
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private volatile Uri tree;
    private volatile String label = "";

    DriveStore(Activity activity, Sink sink) {
        this.activity = activity;
        this.sink = sink;
        String saved = prefs().getString(KEY, null);
        if (saved != null) {
            Uri u = Uri.parse(saved);
            if (stillGranted(u)) tree = u; else prefs().edit().remove(KEY).apply();
        }
        if (tree != null) io.execute(new Runnable() { @Override public void run() { label = nameOf(tree); } });
    }

    private SharedPreferences prefs() { return activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE); }
    private ContentResolver cr() { return activity.getContentResolver(); }

    private boolean stillGranted(Uri u) {
        for (UriPermission p : cr().getPersistedUriPermissions()) {
            if (p.getUri().equals(u) && p.isReadPermission() && p.isWritePermission()) return true;
        }
        return false;
    }

    String state() { return Json.obj("open", tree == null ? 0 : 1, "name", label); }

    /** Opens the system folder picker; the result comes back through MainActivity.onActivityResult -> picked(). */
    void open() {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                try {
                    activity.startActivityForResult(i, PICK);
                } catch (RuntimeException e) {
                    sink.event(Json.obj("t", "drive", "e", "error", "op", "open", "path", "", "msg", "This tablet has no folder picker"));
                }
            }
        });
    }

    void picked(final Uri u) {
        if (u == null) { sink.event(Json.obj("t", "drive", "e", "cancel")); return; }
        io.execute(new Runnable() {
            @Override public void run() {
                try {
                    cr().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                } catch (SecurityException e) {
                    sink.event(Json.obj("t", "drive", "e", "error", "op", "open", "path", "", "msg", "Android did not allow writing there"));
                    return;
                }
                Uri old = tree;
                if (old != null && !old.equals(u)) {
                    try { cr().releasePersistableUriPermission(old, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION); }
                    catch (SecurityException ignored) { /* already gone */ }
                }
                tree = u;
                prefs().edit().putString(KEY, u.toString()).apply();
                label = nameOf(u);
                sink.event(Json.obj("t", "drive", "e", "open", "name", label));
            }
        });
    }

    void list(final String path) {
        io.execute(new Runnable() {
            @Override public void run() {
                try {
                    String dir = resolve(path, true);
                    if (dir == null) throw new FileNotFoundException("no such folder");
                    StringBuilder items = new StringBuilder("[");
                    Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, dir);
                    try (Cursor c = cr().query(kids, new String[] {Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                            Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED}, null, null, null)) {
                        int k = 0;
                        while (c != null && c.moveToNext() && k < 2000) {
                            if (k++ > 0) items.append(',');
                            items.append(Json.obj("n", str(c, 0), "d", Document.MIME_TYPE_DIR.equals(c.getString(1)) ? 1 : 0,
                                    "s", c.isNull(2) ? -1L : c.getLong(2), "m", c.isNull(3) ? 0L : c.getLong(3)));
                        }
                    }
                    items.append(']');
                    String head = Json.obj("t", "drive", "e", "list", "path", path);
                    sink.event(head.substring(0, head.length() - 1) + ",\"items\":" + items + "}");
                } catch (Exception e) {
                    fail("list", path, e);
                }
            }
        });
    }

    void read(final String path) {
        io.execute(new Runnable() {
            @Override public void run() {
                try {
                    String id = resolve(path, false);
                    if (id == null) throw new FileNotFoundException("no such file");
                    Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    try (ParcelFileDescriptor pfd = cr().openFileDescriptor(doc, "r");
                         FileInputStream in = new FileInputStream(pfd.getFileDescriptor())) {
                        byte[] b = new byte[65536];
                        int n;
                        while ((n = in.read(b)) > 0) {
                            out.write(b, 0, n);
                            if (out.size() > MAX_READ) throw new IOException("bigger than 4 MB");
                        }
                    }
                    byte[] all = out.toByteArray();
                    sink.event(Json.obj("t", "drive", "e", "read", "path", path, "b", B64.encode(all, 0, all.length)));
                } catch (Exception e) {
                    fail("read", path, e);
                }
            }
        });
    }

    /** Creates the file (and missing folders) or replaces its contents, then flushes it to the stick before saying "wrote". */
    void write(final String path, final String b64) {
        io.execute(new Runnable() {
            @Override public void run() {
                try {
                    List<String> parts = DrivePath.split(path);
                    if (parts == null || parts.isEmpty()) throw new IllegalArgumentException("not a file name this app will write");
                    byte[] bytes = B64.decode(b64);
                    String dir = rootId();
                    for (int i = 0; i < parts.size() - 1; i++) {
                        String sub = child(dir, parts.get(i), true);
                        if (sub == null) {
                            Uri made = DocumentsContract.createDocument(cr(), DocumentsContract.buildDocumentUriUsingTree(tree, dir),
                                    Document.MIME_TYPE_DIR, parts.get(i));
                            if (made == null) throw new IOException("could not make folder " + parts.get(i));
                            sub = DocumentsContract.getDocumentId(made);
                        }
                        dir = sub;
                    }
                    String name = parts.get(parts.size() - 1);
                    String id = child(dir, name, false);
                    Uri doc;
                    if (id != null) {
                        doc = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    } else {
                        // octet-stream so the provider never appends ".txt" to setup.bat
                        doc = DocumentsContract.createDocument(cr(), DocumentsContract.buildDocumentUriUsingTree(tree, dir),
                                "application/octet-stream", name);
                        if (doc == null) throw new IOException("could not create the file");
                    }
                    writeAll(doc, bytes);
                    sink.event(Json.obj("t", "drive", "e", "wrote", "path", path, "n", bytes.length, "name", label));
                } catch (Exception e) {
                    fail("write", path, e);
                }
            }
        });
    }

    private void writeAll(Uri doc, byte[] bytes) throws IOException {
        ParcelFileDescriptor pfd;
        try { pfd = cr().openFileDescriptor(doc, "rwt"); }
        catch (IllegalArgumentException | FileNotFoundException e) { pfd = cr().openFileDescriptor(doc, "w"); }
        if (pfd == null) throw new IOException("could not open the file for writing");
        try (ParcelFileDescriptor p = pfd; FileOutputStream out = new FileOutputStream(p.getFileDescriptor())) {
            out.getChannel().truncate(0);
            out.getChannel().position(0);
            out.write(bytes);
            out.flush();
            out.getFD().sync();          // on the stick before we say so: the user is about to pull it out
        }
    }

    void forget() {
        Uri old = tree;
        tree = null;
        label = "";
        prefs().edit().remove(KEY).apply();
        if (old != null) {
            try { cr().releasePersistableUriPermission(old, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION); }
            catch (SecurityException ignored) { /* already gone */ }
        }
    }

    void shutdown() { io.shutdownNow(); }

    // ---- lookups -------------------------------------------------------------------------------------------------------------------------------
    private String rootId() throws FileNotFoundException {
        if (tree == null) throw new FileNotFoundException("no drive chosen");
        return DocumentsContract.getTreeDocumentId(tree);
    }

    /** The document id at path (a folder when wantDir), or null. */
    private String resolve(String path, boolean wantDir) throws FileNotFoundException {
        List<String> parts = DrivePath.split(path);
        if (parts == null) throw new IllegalArgumentException("not a path this app will touch");
        String id = rootId();
        for (int i = 0; i < parts.size(); i++) {
            boolean last = i == parts.size() - 1;
            id = child(id, parts.get(i), !last || wantDir);
            if (id == null) return null;
        }
        return id;
    }

    /** FAT and exFAT ignore case, so names match ignoring case, an exact match first. */
    private String child(String parent, String name, boolean dir) {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
        String loose = null;
        try (Cursor c = cr().query(kids, new String[] {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE},
                null, null, null)) {
            while (c != null && c.moveToNext()) {
                boolean isDir = Document.MIME_TYPE_DIR.equals(c.getString(2));
                if (isDir != dir) continue;
                String n = str(c, 1);
                if (n.equals(name)) return c.getString(0);
                if (loose == null && n.equalsIgnoreCase(name)) loose = c.getString(0);
            }
        }
        return loose;
    }

    private String nameOf(Uri t) {
        try (Cursor c = cr().query(DocumentsContract.buildDocumentUriUsingTree(t, DocumentsContract.getTreeDocumentId(t)),
                new String[] {Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return str(c, 0);
        } catch (RuntimeException ignored) { /* the drive is unplugged: no name */ }
        return "";
    }

    private static String str(Cursor c, int i) { String s = c.getString(i); return s == null ? "" : s; }

    private void fail(String op, String path, Exception e) {
        String msg;
        if (e instanceof SecurityException) msg = "Android no longer lets this app use that drive: choose it again";
        else if (e instanceof FileNotFoundException) msg = tree == null ? "Choose the USB drive first" : "Not found (is the drive still plugged in?)";
        else if (e instanceof IllegalArgumentException) msg = e.getMessage() == null ? "Not a name this app will write" : e.getMessage();
        else msg = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
        sink.event(Json.obj("t", "drive", "e", "error", "op", op, "path", path, "msg", msg));
    }
}
