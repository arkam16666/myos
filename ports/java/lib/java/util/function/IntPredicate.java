package java.util.function;

public interface IntPredicate {
    boolean test(int value);

    default IntPredicate negate() {
        final IntPredicate self = this;
        return new IntPredicate() {
            public boolean test(int v) { return !self.test(v); }
        };
    }

    default IntPredicate and(final IntPredicate other) {
        final IntPredicate self = this;
        return new IntPredicate() {
            public boolean test(int v) { return self.test(v) && other.test(v); }
        };
    }

    default IntPredicate or(final IntPredicate other) {
        final IntPredicate self = this;
        return new IntPredicate() {
            public boolean test(int v) { return self.test(v) || other.test(v); }
        };
    }
}
