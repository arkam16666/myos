package java.nio.file;

public final class Path implements Comparable<Path> {
    private final String p;

    private Path(String p) { this.p = p.replace('\\', '/'); }

    public static Path of(String first, String... more) {
        StringBuilder sb = new StringBuilder(first);
        for (String m : more) {
            if (sb.length() > 0 && sb.charAt(sb.length() - 1) != '/') sb.append('/');
            sb.append(m);
        }
        return new Path(sb.toString());
    }

    public Path getFileName() {
        int i = p.lastIndexOf('/');
        return new Path(i < 0 ? p : p.substring(i + 1));
    }

    public Path getParent() {
        int i = p.lastIndexOf('/');
        return i <= 0 ? (i == 0 ? new Path("/") : null) : new Path(p.substring(0, i));
    }

    public Path resolve(String other) { return of(p, other); }

    public Path resolve(Path other) { return of(p, other.p); }

    public boolean isAbsolute() { return p.startsWith("/"); }

    public Path toAbsolutePath() { return new Path(new java.io.File(p).getAbsolutePath()); }

    public java.io.File toFile() { return new java.io.File(p); }

    public int compareTo(Path other) { return p.compareTo(other.p); }

    public boolean equals(Object o) { return o instanceof Path && ((Path) o).p.equals(p); }

    public int hashCode() { return p.hashCode(); }

    public String toString() { return p; }
}
