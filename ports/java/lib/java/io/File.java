package java.io;

public class File implements Serializable, Comparable<File> {
    public static final char separatorChar = '/';
    public static final String separator = "/";
    public static final char pathSeparatorChar = ':';
    public static final String pathSeparator = ":";
    private final String path;

    public File(String pathname) {
        if (pathname == null) throw new NullPointerException();
        String p = pathname.replace('\\', '/');
        while (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
        path = p;
    }

    public File(String parent, String child) { this(parent == null || parent.isEmpty() ? child : parent + "/" + child); }

    public File(File parent, String child) { this(parent == null ? null : parent.getPath(), child); }

    private static native long stat0(String path);

    private static native boolean delete0(String path);

    private static native boolean mkdir0(String path);

    private static native boolean rename0(String from, String to);

    private static native String[] list0(String path);

    public String getName() {
        int i = path.lastIndexOf('/');
        return i < 0 ? path : path.substring(i + 1);
    }

    public String getParent() {
        int i = path.lastIndexOf('/');
        if (i < 0) return null;
        return i == 0 ? "/" : path.substring(0, i);
    }

    public File getParentFile() {
        String p = getParent();
        return p == null ? null : new File(p);
    }

    public String getPath() { return path; }

    public boolean isAbsolute() { return path.startsWith("/"); }

    public String getAbsolutePath() {
        if (isAbsolute()) return path;
        String cwd = System.getProperty("user.dir");
        if (cwd == null || cwd.equals(".")) return path;
        return cwd.endsWith("/") ? cwd + path : cwd + "/" + path;
    }

    public File getAbsoluteFile() { return new File(getAbsolutePath()); }

    public String getCanonicalPath() throws IOException { return getAbsolutePath(); }

    public File getCanonicalFile() throws IOException { return getAbsoluteFile(); }

    public boolean exists() { return stat0(path) >= 0; }

    public boolean isDirectory() { return stat0(path) == (1L << 62); }

    public boolean isFile() {
        long s = stat0(path);
        return s >= 0 && s != (1L << 62);
    }

    public boolean canRead() { return exists(); }

    public boolean canWrite() { return true; }

    public boolean isHidden() { return getName().startsWith("."); }

    public long length() {
        long s = stat0(path);
        return s < 0 || s == (1L << 62) ? 0 : s;
    }

    public long lastModified() { return 0; }

    public boolean createNewFile() throws IOException {
        if (exists()) return false;
        new FileOutputStream(path).close();
        return true;
    }

    public boolean delete() { return delete0(path); }

    public void deleteOnExit() {}

    public String[] list() { return list0(path); }

    public File[] listFiles() {
        String[] names = list();
        if (names == null) return null;
        File[] f = new File[names.length];
        for (int i = 0; i < names.length; i++) f[i] = new File(this, names[i]);
        return f;
    }

    public boolean mkdir() { return mkdir0(path); }

    public boolean mkdirs() {
        if (exists()) return false;
        File parent = getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        return mkdir();
    }

    public boolean renameTo(File dest) { return rename0(path, dest.path); }

    public java.nio.file.Path toPath() { return java.nio.file.Path.of(path); }

    public int compareTo(File other) { return path.compareTo(other.path); }

    public boolean equals(Object obj) { return obj instanceof File && ((File) obj).path.equals(path); }

    public int hashCode() { return path.hashCode() ^ 1234321; }

    public String toString() { return path; }
}
