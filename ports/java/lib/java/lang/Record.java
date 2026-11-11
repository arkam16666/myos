package java.lang;

public abstract class Record {
    protected Record() {}

    public abstract boolean equals(Object obj);

    public abstract int hashCode();

    public abstract String toString();

    /* used by the VM for records (java.lang.runtime.ObjectMethods) */
    static String toStringHelper(String name, String names, Object[] values) {
        StringBuilder sb = new StringBuilder(name).append('[');
        String[] n = names.isEmpty() ? new String[0] : names.split(";");
        for (int i = 0; i < values.length; i++) {
            if (i > 0) sb.append(", ");
            sb.append(i < n.length ? n[i] : "?").append('=').append(values[i]);
        }
        return sb.append(']').toString();
    }

    static int hashCodeHelper(Object[] values) {
        int h = 0;
        for (int i = 0; i < values.length; i++) h = h * 31 + (values[i] == null ? 0 : values[i].hashCode());
        return h;
    }

    static boolean equalsHelper(Object[] a, Object[] b) {
        for (int i = 0; i < a.length; i++) {
            Object x = a[i], y = b[i];
            if (x == null ? y != null : !x.equals(y)) return false;
        }
        return true;
    }
}
