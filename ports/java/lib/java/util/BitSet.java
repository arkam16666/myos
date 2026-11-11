package java.util;

public class BitSet implements Cloneable, java.io.Serializable {
    private long[] words;

    public BitSet() { words = new long[1]; }

    public BitSet(int nbits) { words = new long[Math.max(1, (nbits + 63) >> 6)]; }

    private void ensure(int wordIndex) {
        if (wordIndex >= words.length) words = Arrays.copyOf(words, Math.max(words.length * 2, wordIndex + 1));
    }

    public void set(int i) {
        if (i < 0) throw new IndexOutOfBoundsException("bitIndex < 0: " + i);
        ensure(i >> 6);
        words[i >> 6] |= 1L << i;
    }

    public void set(int i, boolean value) {
        if (value) set(i);
        else clear(i);
    }

    public void set(int from, int to) { for (int i = from; i < to; i++) set(i); }

    public void clear(int i) {
        if ((i >> 6) < words.length) words[i >> 6] &= ~(1L << i);
    }

    public void clear() { Arrays.fill(words, 0); }

    public void flip(int i) {
        ensure(i >> 6);
        words[i >> 6] ^= 1L << i;
    }

    public boolean get(int i) {
        if (i < 0) throw new IndexOutOfBoundsException("bitIndex < 0: " + i);
        return (i >> 6) < words.length && (words[i >> 6] & (1L << i)) != 0;
    }

    public int nextSetBit(int from) {
        for (int i = from; i < words.length * 64; i++) if (get(i)) return i;
        return -1;
    }

    public int nextClearBit(int from) {
        int i = from;
        while (get(i)) i++;
        return i;
    }

    public int cardinality() {
        int n = 0;
        for (long w : words) n += Long.bitCount(w);
        return n;
    }

    public int length() {
        for (int i = words.length - 1; i >= 0; i--) if (words[i] != 0) return i * 64 + 64 - Long.numberOfLeadingZeros(words[i]);
        return 0;
    }

    public int size() { return words.length * 64; }

    public boolean isEmpty() { return length() == 0; }

    public void and(BitSet set) {
        for (int i = 0; i < words.length; i++) words[i] &= i < set.words.length ? set.words[i] : 0;
    }

    public void or(BitSet set) {
        ensure(set.words.length - 1);
        for (int i = 0; i < set.words.length; i++) words[i] |= set.words[i];
    }

    public void xor(BitSet set) {
        ensure(set.words.length - 1);
        for (int i = 0; i < set.words.length; i++) words[i] ^= set.words[i];
    }

    public void andNot(BitSet set) {
        for (int i = 0; i < Math.min(words.length, set.words.length); i++) words[i] &= ~set.words[i];
    }

    public java.util.stream.IntStream stream() {
        int[] a = new int[cardinality()];
        int k = 0;
        for (int i = nextSetBit(0); i >= 0; i = nextSetBit(i + 1)) a[k++] = i;
        return java.util.stream.IntStream.of(a);
    }

    public boolean equals(Object o) {
        if (!(o instanceof BitSet)) return false;
        BitSet b = (BitSet) o;
        int n = Math.max(words.length, b.words.length);
        for (int i = 0; i < n; i++)
            if ((i < words.length ? words[i] : 0) != (i < b.words.length ? b.words[i] : 0)) return false;
        return true;
    }

    public int hashCode() {
        long h = 1234;
        for (int i = words.length; --i >= 0;) h ^= words[i] * (i + 1);
        return (int) ((h >> 32) ^ h);
    }

    public Object clone() {
        BitSet b = new BitSet();
        b.words = words.clone();
        return b;
    }

    public String toString() {
        StringBuilder sb = new StringBuilder("{");
        boolean first = true;
        for (int i = nextSetBit(0); i >= 0; i = nextSetBit(i + 1)) {
            if (!first) sb.append(", ");
            sb.append(i);
            first = false;
        }
        return sb.append('}').toString();
    }
}
