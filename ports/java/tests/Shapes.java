import java.util.ArrayList;
import java.util.List;

interface Shape extends Comparable<Shape> {
    double area();

    String name();

    default String describe() { return name() + " with area " + String.format("%.2f", area()); }

    static Shape unit() { return new Square(1); }

    default int compareTo(Shape o) { return Double.compare(area(), o.area()); }
}

abstract class Base implements Shape {
    static int created;
    protected final String label;

    static {
        System.out.println("Base static init");
    }

    {
        created++;
    }

    Base(String label) { this.label = label; }

    public String name() { return label; }

    public String toString() { return getClass().getSimpleName() + "(" + label + ")"; }
}

class Circle extends Base {
    final double r;

    static {
        System.out.println("Circle static init");
    }

    Circle(double r) {
        super("circle");
        this.r = r;
    }

    public double area() { return Math.PI * r * r; }
}

class Square extends Base {
    final double side;

    Square(double side) {
        super("square");
        this.side = side;
    }

    public double area() { return side * side; }

    public String name() { return "big " + super.name(); }

    public boolean equals(Object o) { return o instanceof Square && ((Square) o).side == side; }

    public int hashCode() { return Double.hashCode(side); }
}

public class Shapes {
    private int counter = 0;
    private static String greeting = initGreeting();

    static String initGreeting() {
        System.out.println("Shapes static init");
        return "hi";
    }

    class Inner {
        int bump() { return ++counter; }
    }

    static class Nested {
        String hello() { return greeting + " from nested"; }
    }

    interface Visitor<R> {
        R visit(Shape s);
    }

    static <T extends Comparable<T>> T largest(List<T> items) {
        T best = items.get(0);
        for (T t : items) if (t.compareTo(best) > 0) best = t;
        return best;
    }

    static int sum(int... xs) {
        int s = 0;
        for (int x : xs) s += x;
        return s;
    }

    static String kind(Object o) {
        if (o instanceof Circle) return "circle!";
        if (o instanceof Shape) return "some shape";
        return "not a shape";
    }

    public static void main(String[] args) {
        System.out.println("main starts");
        List<Shape> shapes = new ArrayList<>();
        shapes.add(new Circle(1.5));
        shapes.add(new Square(2));
        shapes.add(Shape.unit());
        for (Shape s : shapes) System.out.println(s + ": " + s.describe());
        System.out.println("created " + Base.created + " largest " + largest(shapes));
        java.util.Collections.sort(shapes);
        System.out.println("sorted " + shapes);
        System.out.println(new Square(2).equals(new Square(2)) + " " + (new Square(2).hashCode() == new Square(2).hashCode()));
        Shapes outer = new Shapes();
        Shapes.Inner in1 = outer.new Inner();
        Inner in2 = outer.new Inner();
        in1.bump();
        in2.bump();
        System.out.println("counter " + outer.counter + " " + new Nested().hello());
        Visitor<String> v = new Visitor<String>() {
            int calls;

            public String visit(Shape s) { return "visited " + s.name() + " #" + (++calls); }
        };
        for (Shape s : shapes) System.out.println(v.visit(s));
        class Local implements Runnable {
            final int n;

            Local(int n) { this.n = n; }

            public void run() { System.out.println("local run " + n + " " + greeting); }
        }
        new Local(7).run();
        System.out.println(sum() + " " + sum(1) + " " + sum(1, 2, 3, 4));
        System.out.println(kind(new Circle(1)) + " " + kind(new Square(1)) + " " + kind("x"));
        Object o = shapes;
        System.out.println((o instanceof List) + " " + (o instanceof java.util.Collection) + " " + (o instanceof Runnable));
        Shape[] arr = new Shape[2];
        Object[] objs = arr;
        System.out.println(objs.getClass().getSimpleName() + " " + arr.getClass().getName() + " " + int[].class.getName() + " " + new int[0][0].getClass().getSimpleName());
        System.out.println(new Circle(2).getClass().getName() + " " + Shape.class.isInterface() + " " + Base.class.getSuperclass().getName());
        Runnable r = new Runnable() {
            public void run() { System.out.println("anonymous runnable, class name empty: " + getClass().getSimpleName().isEmpty()); }
        };
        r.run();
        Thread t = new Thread(() -> System.out.println("thread body runs"));
        t.start();
        try {
            t.join();
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }
        System.out.println("thread joined, main is " + Thread.currentThread().getName());
        Object lock = new Object();
        synchronized (lock) {
            System.out.println("in synchronized block");
        }
        Integer boxed = 127, boxed2 = 127, big1 = 1000, big2 = 1000;
        System.out.println((boxed == boxed2) + " " + big1.equals(big2) + " " + (big1 + big2));
        Long lg = 5L;
        Double dd = 2.5;
        Character ch = 'q';
        Boolean bb = true;
        System.out.println(lg * 2 + " " + dd * 2 + " " + (char) (ch + 1) + " " + !bb);
        List<Integer> li = new ArrayList<>();
        li.add(5);
        li.add(10);
        int total = 0;
        for (int x : li) total += x;
        System.out.println("unboxed total " + total);
    }
}
