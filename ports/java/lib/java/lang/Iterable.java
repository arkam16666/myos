package java.lang;

import java.util.Iterator;
import java.util.function.Consumer;

public interface Iterable<T> {
    Iterator<T> iterator();

    default void forEach(Consumer<? super T> action) {
        for (Iterator<T> it = iterator(); it.hasNext();) action.accept(it.next());
    }
}
