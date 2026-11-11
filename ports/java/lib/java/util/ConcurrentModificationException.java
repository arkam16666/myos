package java.util;

public class ConcurrentModificationException extends RuntimeException {
    public ConcurrentModificationException() { super(); }

    public ConcurrentModificationException(String message) { super(message); }

    public ConcurrentModificationException(String message, Throwable cause) { super(message, cause); }

    public ConcurrentModificationException(Throwable cause) { super(cause); }
}
