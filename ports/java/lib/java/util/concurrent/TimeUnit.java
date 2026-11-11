package java.util.concurrent;

public enum TimeUnit {
    NANOSECONDS(1L), MICROSECONDS(1000L), MILLISECONDS(1000000L), SECONDS(1000000000L), MINUTES(60000000000L),
    HOURS(3600000000000L), DAYS(86400000000000L);

    private final long nanos;

    TimeUnit(long nanos) { this.nanos = nanos; }

    public long convert(long duration, TimeUnit unit) { return duration * unit.nanos / nanos; }

    public long toNanos(long d) { return d * nanos; }

    public long toMicros(long d) { return d * nanos / 1000L; }

    public long toMillis(long d) { return d * nanos / 1000000L; }

    public long toSeconds(long d) { return d * nanos / 1000000000L; }

    public long toMinutes(long d) { return d * nanos / 60000000000L; }

    public long toHours(long d) { return d * nanos / 3600000000000L; }

    public long toDays(long d) { return d * nanos / 86400000000000L; }

    public void sleep(long timeout) throws InterruptedException { Thread.sleep(toMillis(timeout)); }
}
