package java.lang;

public interface CharSequence {
    int length();

    char charAt(int index);

    CharSequence subSequence(int start, int end);

    String toString();

    default boolean isEmpty() { return length() == 0; }

    default java.util.stream.IntStream chars() {
        int n = length();
        int[] a = new int[n];
        for (int i = 0; i < n; i++) a[i] = charAt(i);
        return java.util.stream.IntStream.of(a);
    }

    public static int compare(CharSequence a, CharSequence b) {
        int n = Math.min(a.length(), b.length());
        for (int i = 0; i < n; i++) {
            int d = a.charAt(i) - b.charAt(i);
            if (d != 0) return d;
        }
        return a.length() - b.length();
    }
}
