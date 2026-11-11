package java.util.stream;

public interface BaseStream<T, S extends BaseStream<T, S>> extends AutoCloseable {
    java.util.Iterator<T> iterator();

    boolean isParallel();

    S sequential();

    S parallel();

    S unordered();

    S onClose(Runnable closeHandler);

    void close();
}
