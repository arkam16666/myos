package java.util.function;

public interface IntConsumer {
    void accept(int value);

    default IntConsumer andThen(final IntConsumer after) {
        final IntConsumer self = this;
        return new IntConsumer() {
            public void accept(int v) {
                self.accept(v);
                after.accept(v);
            }
        };
    }
}
