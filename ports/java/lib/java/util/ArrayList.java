package java.util;

import java.util.function.Consumer;
import java.util.function.Predicate;
import java.util.function.UnaryOperator;

public class ArrayList<E> extends AbstractList<E> implements List<E>, RandomAccess, Cloneable, java.io.Serializable {
    Object[] elementData;
    int size;

    public ArrayList(int initialCapacity) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal Capacity: " + initialCapacity);
        elementData = new Object[initialCapacity];
    }

    public ArrayList() { elementData = new Object[10]; }

    public ArrayList(Collection<? extends E> c) {
        elementData = c.toArray();
        size = elementData.length;
        if (elementData.getClass() != Object[].class) elementData = Arrays.copyOf(elementData, size, Object[].class);
    }

    public void trimToSize() {
        modCount++;
        if (size < elementData.length) elementData = Arrays.copyOf(elementData, size);
    }

    public void ensureCapacity(int minCapacity) {
        if (minCapacity > elementData.length) {
            modCount++;
            grow(minCapacity);
        }
    }

    private void grow(int minCapacity) {
        int n = elementData.length + (elementData.length >> 1);
        if (n < minCapacity) n = minCapacity;
        if (n < 10) n = 10;
        elementData = Arrays.copyOf(elementData, n);
    }

    public int size() { return size; }

    public boolean isEmpty() { return size == 0; }

    public boolean contains(Object o) { return indexOf(o) >= 0; }

    public int indexOf(Object o) {
        Object[] es = elementData;
        if (o == null) {
            for (int i = 0; i < size; i++) if (es[i] == null) return i;
        } else {
            for (int i = 0; i < size; i++) if (o.equals(es[i])) return i;
        }
        return -1;
    }

    public int lastIndexOf(Object o) {
        Object[] es = elementData;
        for (int i = size - 1; i >= 0; i--) if (o == null ? es[i] == null : o.equals(es[i])) return i;
        return -1;
    }

    @SuppressWarnings("unchecked")
    public Object clone() {
        ArrayList<E> v = new ArrayList<E>(size);
        System.arraycopy(elementData, 0, v.elementData, 0, size);
        v.size = size;
        return v;
    }

    public Object[] toArray() { return Arrays.copyOf(elementData, size); }

    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        if (a.length < size) return (T[]) Arrays.copyOf(elementData, size, a.getClass());
        System.arraycopy(elementData, 0, a, 0, size);
        if (a.length > size) a[size] = null;
        return a;
    }

    private void check(int index) {
        if (index < 0 || index >= size) throw new IndexOutOfBoundsException("Index " + index + " out of bounds for length " + size);
    }

    @SuppressWarnings("unchecked")
    public E get(int index) {
        check(index);
        return (E) elementData[index];
    }

    @SuppressWarnings("unchecked")
    public E set(int index, E element) {
        check(index);
        E old = (E) elementData[index];
        elementData[index] = element;
        return old;
    }

    public boolean add(E e) {
        modCount++;
        if (size == elementData.length) grow(size + 1);
        elementData[size++] = e;
        return true;
    }

    public void add(int index, E element) {
        rangeCheckForAdd(index);
        modCount++;
        if (size == elementData.length) grow(size + 1);
        System.arraycopy(elementData, index, elementData, index + 1, size - index);
        elementData[index] = element;
        size++;
    }

    @SuppressWarnings("unchecked")
    public E remove(int index) {
        check(index);
        modCount++;
        E old = (E) elementData[index];
        int n = size - index - 1;
        if (n > 0) System.arraycopy(elementData, index + 1, elementData, index, n);
        elementData[--size] = null;
        return old;
    }

    public boolean remove(Object o) {
        int i = indexOf(o);
        if (i < 0) return false;
        remove(i);
        return true;
    }

    public void clear() {
        modCount++;
        for (int i = 0; i < size; i++) elementData[i] = null;
        size = 0;
    }

    public boolean addAll(Collection<? extends E> c) {
        Object[] a = c.toArray();
        modCount++;
        int n = a.length;
        if (n == 0) return false;
        if (size + n > elementData.length) grow(size + n);
        System.arraycopy(a, 0, elementData, size, n);
        size += n;
        return true;
    }

    public boolean addAll(int index, Collection<? extends E> c) {
        rangeCheckForAdd(index);
        Object[] a = c.toArray();
        modCount++;
        int n = a.length;
        if (n == 0) return false;
        if (size + n > elementData.length) grow(size + n);
        System.arraycopy(elementData, index, elementData, index + n, size - index);
        System.arraycopy(a, 0, elementData, index, n);
        size += n;
        return true;
    }

    protected void removeRange(int fromIndex, int toIndex) {
        if (fromIndex > toIndex) throw new IndexOutOfBoundsException("From Index: " + fromIndex + " > To Index: " + toIndex);
        modCount++;
        System.arraycopy(elementData, toIndex, elementData, fromIndex, size - toIndex);
        int newSize = size - (toIndex - fromIndex);
        for (int i = newSize; i < size; i++) elementData[i] = null;
        size = newSize;
    }

    public boolean removeAll(Collection<?> c) { return batchRemove(c, false); }

    public boolean retainAll(Collection<?> c) { return batchRemove(c, true); }

    private boolean batchRemove(Collection<?> c, boolean complement) {
        Objects.requireNonNull(c);
        int w = 0;
        for (int r = 0; r < size; r++) if (c.contains(elementData[r]) == complement) elementData[w++] = elementData[r];
        if (w == size) return false;
        for (int i = w; i < size; i++) elementData[i] = null;
        modCount++;
        size = w;
        return true;
    }

    @SuppressWarnings("unchecked")
    public boolean removeIf(Predicate<? super E> filter) {
        int w = 0, expected = modCount;
        for (int r = 0; r < size; r++) {
            if (!filter.test((E) elementData[r])) elementData[w++] = elementData[r];
        }
        if (modCount != expected) throw new ConcurrentModificationException();
        if (w == size) return false;
        for (int i = w; i < size; i++) elementData[i] = null;
        size = w;
        modCount++;
        return true;
    }

    @SuppressWarnings("unchecked")
    public void forEach(Consumer<? super E> action) {
        int expected = modCount;
        for (int i = 0; i < size && modCount == expected; i++) action.accept((E) elementData[i]);
        if (modCount != expected) throw new ConcurrentModificationException();
    }

    @SuppressWarnings("unchecked")
    public void replaceAll(UnaryOperator<E> operator) {
        for (int i = 0; i < size; i++) elementData[i] = operator.apply((E) elementData[i]);
        modCount++;
    }

    @SuppressWarnings("unchecked")
    public void sort(Comparator<? super E> c) {
        int expected = modCount;
        Arrays.sort((E[]) elementData, 0, size, c);
        if (modCount != expected) throw new ConcurrentModificationException();
        modCount++;
    }

    public Iterator<E> iterator() { return new Itr(); }

    private class Itr implements Iterator<E> {
        int cursor, lastRet = -1, expectedModCount = modCount;

        public boolean hasNext() { return cursor != size; }

        @SuppressWarnings("unchecked")
        public E next() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            int i = cursor;
            if (i >= size) throw new NoSuchElementException();
            cursor = i + 1;
            return (E) elementData[lastRet = i];
        }

        public void remove() {
            if (lastRet < 0) throw new IllegalStateException();
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            ArrayList.this.remove(lastRet);
            cursor = lastRet;
            lastRet = -1;
            expectedModCount = modCount;
        }
    }
}
