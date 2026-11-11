package java.nio.charset;

public class Charset implements Comparable<Charset> {
    private final String name;

    Charset(String name) { this.name = name; }

    public static Charset forName(String charsetName) {
        Charset c = lookup(charsetName);
        if (c == null) throw new IllegalArgumentException("Unsupported charset: " + charsetName);
        return c;
    }

    public static Charset defaultCharset() { return StandardCharsets.UTF_8; }

    public static boolean isSupported(String charsetName) { return lookup(charsetName) != null; }

    /* for String: null = unknown */
    public static Charset lookup(String n) {
        String u = n.toUpperCase().replace("_", "-");
        if (u.equals("UTF-8") || u.equals("UTF8")) return StandardCharsets.UTF_8;
        if (u.equals("ISO-8859-1") || u.equals("LATIN1") || u.equals("ISO8859-1")) return StandardCharsets.ISO_8859_1;
        if (u.equals("US-ASCII") || u.equals("ASCII")) return StandardCharsets.US_ASCII;
        return null;
    }

    public boolean isUtf8() { return name.equals("UTF-8"); }

    public final String name() { return name; }

    public String displayName() { return name; }

    public final String toString() { return name; }

    public final int compareTo(Charset that) { return name.compareToIgnoreCase(that.name); }
}
