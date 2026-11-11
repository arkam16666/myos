import java.util.*;
import java.util.function.*;
import java.util.stream.*;

/* compiled with javac's default target: invokedynamic string concatenation,
 * lambdas, records, switch expressions, text blocks, var, patterns */
public class Modern {
    record Point(int x, int y) {
        Point {
            if (x < 0) throw new IllegalArgumentException("negative x");
        }

        double dist() { return Math.sqrt(x * x + y * y); }
    }

    record Person(String name, int age, double score) {}

    sealed interface Expr permits Num, Add, Mul {}

    record Num(int v) implements Expr {}

    record Add(Expr l, Expr r) implements Expr {}

    record Mul(Expr l, Expr r) implements Expr {}

    static int eval(Expr e) {
        return switch (e) {
            case Num n -> n.v();
            case Add a -> eval(a.l()) + eval(a.r());
            case Mul m -> eval(m.l()) * eval(m.r());
        };
    }

    enum Day { MON, TUE, SAT, SUN }

    static String kind(Day d) {
        return switch (d) {
            case SAT, SUN -> "weekend";
            default -> "weekday";
        };
    }

    static String describe(Object o) {
        if (o instanceof String s && s.length() > 3) return "long string " + s;
        if (o instanceof Integer i) return "int " + (i + 1);
        return "other " + o;
    }

    static String typeOf(Object o) {
        return switch (o) {
            case null -> "null!";
            case Integer i when i > 10 -> "big int";
            case Integer i -> "small int " + i;
            case String s -> "string of " + s.length();
            default -> "something";
        };
    }

    public static void main(String[] args) {
        int a = 6;
        long b = 7_000_000_000L;
        double c = 1.5;
        char ch = 'Z';
        boolean ok = true;
        String nul = null;
        System.out.println("a=" + a + " b=" + b + " c=" + c + " ch=" + ch + " ok=" + ok + " nul=" + nul + " " + a * c);
        var list = new ArrayList<String>(List.of("pear", "apple", "fig", "banana", "cherry"));
        list.sort(Comparator.comparing(String::length).thenComparing(Comparator.naturalOrder()));
        System.out.println(list);
        list.forEach(s -> System.out.print(s.charAt(0)));
        System.out.println();
        Function<Integer, Integer> square = x -> x * x;
        Function<Integer, Integer> plus1 = x -> x + 1;
        BiFunction<Integer, Integer, Integer> add = Integer::sum;
        Supplier<List<String>> maker = ArrayList::new;
        UnaryOperator<String> up = String::toUpperCase;
        Predicate<String> empty = String::isEmpty;
        System.out.println(square.andThen(plus1).apply(5) + " " + add.apply(2, 3) + " " + maker.get() + " " + up.apply("shout") + " " + empty.negate().test(""));
        int base = 100;
        IntBinaryOperator withBase = (x, y) -> x + y + base;
        ToIntFunction<String> len = String::length;
        IntFunction<String> stars = "*"::repeat;
        System.out.println(withBase.applyAsInt(1, 2) + " " + len.applyAsInt("four") + " " + stars.apply(5));
        List<Integer> nums = IntStream.rangeClosed(1, 10).boxed().collect(Collectors.toList());
        System.out.println(nums.stream().filter(n -> n % 2 == 0).map(n -> n * n).collect(Collectors.toList()));
        System.out.println(nums.stream().mapToInt(Integer::intValue).sum() + " " + nums.stream().reduce(0, Integer::sum) + " " + IntStream.of(3, 1, 4, 1, 5).max().getAsInt());
        System.out.println(Stream.of("a", "bb", "cc", "ddd").collect(Collectors.groupingBy(String::length)));
        System.out.println(Stream.of("x", "y", "z").collect(Collectors.joining(", ", "[", "]")) + " " + Stream.iterate(1, x -> x * 2).limit(8).toList());
        System.out.println(list.stream().map(String::length).distinct().sorted(Comparator.reverseOrder()).toList() + " " + list.stream().anyMatch(s -> s.startsWith("b")));
        System.out.println(IntStream.range(0, 5).mapToObj(i -> "n" + i).collect(Collectors.joining("|")) + " " + nums.stream().mapToInt(x -> x).average().getAsDouble());
        Map<Boolean, Long> parts = nums.stream().collect(Collectors.partitioningBy(n -> n > 7, Collectors.counting()));
        System.out.println(parts + " " + Stream.of("b", "a", "c").sorted().findFirst().get() + " " + Optional.empty().isPresent() + " " + Optional.of(3).map(x -> x * 2));
        Map<String, Integer> lengths = list.stream().collect(Collectors.toMap(s -> s, String::length, (x, y) -> x, TreeMap::new));
        System.out.println(lengths);
        var p = new Point(3, 4);
        var q = new Point(3, 4);
        System.out.println(p + " " + p.x() + " " + p.dist() + " " + p.equals(q) + " " + (p.hashCode() == q.hashCode()) + " " + p.equals(new Point(4, 3)));
        try {
            new Point(-1, 0);
        } catch (IllegalArgumentException e) {
            System.out.println("record check: " + e.getMessage());
        }
        var people = List.of(new Person("Ann", 30, 88.5), new Person("Bob", 25, 92.0), new Person("Cid", 35, 75.25));
        System.out.println(people.stream().max(Comparator.comparingDouble(Person::score)).get().name() + " " + people.get(2));
        System.out.println(people.stream().collect(Collectors.averagingInt(Person::age)) + " " + people.stream().map(Person::name).collect(Collectors.joining("+")));
        System.out.println(eval(new Add(new Num(2), new Mul(new Num(3), new Num(4)))));
        for (Day d : Day.values()) System.out.print(kind(d) + " ");
        System.out.println();
        System.out.println(describe("hello") + ", " + describe(41) + ", " + describe("hi"));
        System.out.println(typeOf(5) + ", " + typeOf(50) + ", " + typeOf("abc") + ", " + typeOf(null) + ", " + typeOf(2.0));
        int code = 3;
        String word = switch (code) {
            case 1 -> "one";
            case 2, 3 -> {
                String t = "two";
                yield t + " or three";
            }
            default -> "many";
        };
        System.out.println(word);
        String block = """
            Text blocks
              keep indentation
            and "quotes".
            """;
        System.out.print(block);
        Runnable r = () -> System.out.println("runnable lambda");
        r.run();
        Comparator<Person> byAge = Comparator.comparingInt(Person::age);
        System.out.println(people.stream().sorted(byAge.reversed()).map(Person::name).toList());
        int[] counter = { 0 };
        IntStream.range(0, 4).forEach(i -> counter[0] += i);
        System.out.println("counter " + counter[0]);
        BinaryOperator<Integer> maxOp = BinaryOperator.maxBy(Comparator.naturalOrder());
        System.out.println(maxOp.apply(3, 9) + " " + Stream.of(1, 2, 3, 4).collect(Collectors.summingInt(x -> x)));
        List<List<Integer>> nested = List.of(List.of(1, 2), List.of(3), List.of());
        System.out.println(nested.stream().flatMap(List::stream).map(String::valueOf).collect(Collectors.joining(",")));
        System.out.println(String.valueOf(new char[] { 'o', 'k' }) + " " + "%s=%d".formatted("k", 9) + " " + Objects.toString(null, "none"));
    }
}
