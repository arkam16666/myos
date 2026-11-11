package java.util.concurrent;

public class ThreadLocalRandom extends java.util.Random {
    private static final ThreadLocalRandom instance = new ThreadLocalRandom();

    private ThreadLocalRandom() {}

    public static ThreadLocalRandom current() { return instance; }
}
