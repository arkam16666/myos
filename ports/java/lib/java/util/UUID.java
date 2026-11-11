package java.util;

public final class UUID implements java.io.Serializable, Comparable<UUID> {
    private final long msb, lsb;
    private static Random rng;

    public UUID(long mostSigBits, long leastSigBits) {
        msb = mostSigBits;
        lsb = leastSigBits;
    }

    public static UUID randomUUID() {
        if (rng == null) rng = new Random();
        long m = rng.nextLong(), l = rng.nextLong();
        m = (m & ~0xF000L) | 0x4000L;
        l = (l & 0x3FFFFFFFFFFFFFFFL) | 0x8000000000000000L;
        return new UUID(m, l);
    }

    private static String hex(long v, int digits) {
        String s = Long.toHexString(v);
        while (s.length() < digits) s = "0" + s;
        return s.substring(s.length() - digits);
    }

    public long getMostSignificantBits() { return msb; }

    public long getLeastSignificantBits() { return lsb; }

    public String toString() {
        return hex(msb >>> 32, 8) + "-" + hex(msb >>> 16, 4) + "-" + hex(msb, 4) + "-" + hex(lsb >>> 48, 4) + "-" + hex(lsb, 12);
    }

    public int hashCode() {
        long h = msb ^ lsb;
        return ((int) (h >> 32)) ^ (int) h;
    }

    public boolean equals(Object o) { return o instanceof UUID && ((UUID) o).msb == msb && ((UUID) o).lsb == lsb; }

    public int compareTo(UUID o) {
        int c = Long.compare(msb, o.msb);
        return c != 0 ? c : Long.compare(lsb, o.lsb);
    }
}
