public class Enums {
    enum Color { RED, GREEN, BLUE }

    enum Planet {
        MERCURY(3.303e+23, 2.4397e6), EARTH(5.976e+24, 6.37814e6), JUPITER(1.9e+27, 7.1492e7);

        private final double mass, radius;

        Planet(double mass, double radius) {
            this.mass = mass;
            this.radius = radius;
        }

        double surfaceGravity() { return 6.67300E-11 * mass / (radius * radius); }
    }

    enum Op {
        PLUS("+") {
            int apply(int a, int b) { return a + b; }
        },
        TIMES("*") {
            int apply(int a, int b) { return a * b; }
        };

        final String symbol;

        Op(String symbol) { this.symbol = symbol; }

        abstract int apply(int a, int b);
    }

    static String mood(Color c) {
        switch (c) {
            case RED:
                return "angry";
            case GREEN:
                return "calm";
            default:
                return "sad";
        }
    }

    static int days(int month) {
        switch (month) {
            case 2: return 28;
            case 4: case 6: case 9: case 11: return 30;
            default: return 31;
        }
    }

    static String sparse(int v) {
        switch (v) {
            case -100: return "minus hundred";
            case 7: return "seven";
            case 1000000: return "million";
            default: return "other";
        }
    }

    public static void main(String[] args) {
        for (Color c : Color.values()) System.out.println(c + " " + c.ordinal() + " " + c.name() + " " + mood(c));
        System.out.println(Color.valueOf("GREEN").compareTo(Color.BLUE) + " " + (Color.RED == Color.valueOf("RED")));
        try {
            Color.valueOf("PINK");
        } catch (IllegalArgumentException e) {
            System.out.println(e.getMessage());
        }
        for (Planet p : Planet.values()) System.out.printf("%s %.2f%n", p, p.surfaceGravity());
        for (Op op : Op.values()) System.out.println(op + " " + op.symbol + " " + op.apply(6, 7) + " " + op.getDeclaringClass().getSimpleName());
        int total = 0;
        for (int m = 1; m <= 12; m++) total += days(m);
        System.out.println("days " + total + " " + sparse(7) + " " + sparse(-100) + " " + sparse(1000000) + " " + sparse(3));
        java.util.Map<Color, Integer> counts = new java.util.HashMap<>();
        counts.put(Color.BLUE, 3);
        System.out.println(counts.get(Color.BLUE) + " " + Color.class.isEnum() + " " + Color.RED.getClass().getName());
        char grade = 'B';
        switch (grade) {
            case 'A': System.out.println("excellent"); break;
            case 'B':
            case 'C': System.out.println("good"); // fall through
            case 'D': System.out.println("passed"); break;
            default: System.out.println("invalid");
        }
    }
}
