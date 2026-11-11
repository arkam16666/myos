package java.util;

/* a set of enum constants, kept in ordinal order */
public abstract class EnumSet<E extends Enum<E>> extends AbstractSet<E> implements Cloneable, java.io.Serializable {
    EnumSet() {}

    static final class Impl<E extends Enum<E>> extends EnumSet<E> {
        final Class<E> type;
        final E[] universe;
        final boolean[] in;
        int size;

        Impl(Class<E> type) {
            this.type = type;
            universe = type.getEnumConstants();
            in = new boolean[universe.length];
        }

        public int size() { return size; }

        public boolean contains(Object o) {
            return o instanceof Enum && ((Enum<?>) o).getDeclaringClass() == type && in[((Enum<?>) o).ordinal()];
        }

        public boolean add(E e) {
            if (e.getDeclaringClass() != type) throw new ClassCastException(e.getClass() + " != " + type);
            int i = e.ordinal();
            if (in[i]) return false;
            in[i] = true;
            size++;
            return true;
        }

        public boolean remove(Object o) {
            if (!contains(o)) return false;
            in[((Enum<?>) o).ordinal()] = false;
            size--;
            return true;
        }

        public void clear() {
            Arrays.fill(in, false);
            size = 0;
        }

        public Iterator<E> iterator() {
            return new Iterator<E>() {
                int next = advance(0), last = -1;

                int advance(int i) {
                    while (i < universe.length && !in[i]) i++;
                    return i;
                }

                public boolean hasNext() { return next < universe.length; }

                public E next() {
                    if (next >= universe.length) throw new NoSuchElementException();
                    last = next;
                    next = advance(next + 1);
                    return universe[last];
                }

                public void remove() {
                    if (last < 0) throw new IllegalStateException();
                    if (in[last]) {
                        in[last] = false;
                        size--;
                    }
                    last = -1;
                }
            };
        }
    }

    public static <E extends Enum<E>> EnumSet<E> noneOf(Class<E> elementType) { return new Impl<E>(elementType); }

    public static <E extends Enum<E>> EnumSet<E> allOf(Class<E> elementType) {
        EnumSet<E> s = noneOf(elementType);
        for (E e : elementType.getEnumConstants()) s.add(e);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> copyOf(EnumSet<E> s) { return s.clone(); }

    public static <E extends Enum<E>> EnumSet<E> copyOf(Collection<E> c) {
        if (c instanceof EnumSet) return ((EnumSet<E>) c).clone();
        if (c.isEmpty()) throw new IllegalArgumentException("Collection is empty");
        Iterator<E> it = c.iterator();
        E first = it.next();
        EnumSet<E> s = noneOf(first.getDeclaringClass());
        s.addAll(c);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> complementOf(EnumSet<E> s) {
        Impl<E> src = (Impl<E>) s;
        EnumSet<E> r = noneOf(src.type);
        for (E e : src.universe) if (!src.contains(e)) r.add(e);
        return r;
    }

    public static <E extends Enum<E>> EnumSet<E> of(E e) {
        EnumSet<E> s = noneOf(e.getDeclaringClass());
        s.add(e);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> of(E e1, E e2) {
        EnumSet<E> s = of(e1);
        s.add(e2);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> of(E e1, E e2, E e3) {
        EnumSet<E> s = of(e1, e2);
        s.add(e3);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> of(E e1, E e2, E e3, E e4) {
        EnumSet<E> s = of(e1, e2, e3);
        s.add(e4);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> of(E e1, E e2, E e3, E e4, E e5) {
        EnumSet<E> s = of(e1, e2, e3, e4);
        s.add(e5);
        return s;
    }

    @SafeVarargs
    public static <E extends Enum<E>> EnumSet<E> of(E first, E... rest) {
        EnumSet<E> s = of(first);
        for (E e : rest) s.add(e);
        return s;
    }

    public static <E extends Enum<E>> EnumSet<E> range(E from, E to) {
        EnumSet<E> s = noneOf(from.getDeclaringClass());
        for (E e : from.getDeclaringClass().getEnumConstants())
            if (e.ordinal() >= from.ordinal() && e.ordinal() <= to.ordinal()) s.add(e);
        return s;
    }

    public EnumSet<E> clone() {
        Impl<E> src = (Impl<E>) this;
        EnumSet<E> r = noneOf(src.type);
        r.addAll(this);
        return r;
    }
}
