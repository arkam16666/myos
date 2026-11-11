public class Numbers {
    static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

    static long fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

    public static void main(String[] args) {
        // int
        int big = Integer.MAX_VALUE;
        System.out.println(big + 1);
        System.out.println(Integer.MIN_VALUE / -1);
        System.out.println(Integer.MIN_VALUE % -1);
        System.out.println(-7 / 2 + " " + -7 % 2 + " " + 7 / -2 + " " + 7 % -2);
        System.out.println((1 << 31) + " " + (1 << 32) + " " + (-16 >> 2) + " " + (-16 >>> 28));
        System.out.println(Math.abs(Integer.MIN_VALUE));
        System.out.println(fib(20));
        // long
        long lmax = Long.MAX_VALUE;
        System.out.println(lmax + " " + (lmax + 1) + " " + Long.MIN_VALUE);
        System.out.println(fact(20));
        System.out.println(fact(21));
        System.out.println((-1L >>> 1) + " " + (1L << 63) + " " + (5L << 66));
        System.out.println(123456789L * 987654321L);
        System.out.println(Long.MIN_VALUE / -1 + " " + Long.MAX_VALUE / 7 + " " + Long.MIN_VALUE % 10);
        System.out.println(Long.parseLong("-9223372036854775808") + " " + Long.toHexString(-1L) + " " + Long.toBinaryString(10));
        // double / float
        System.out.println(0.1 + 0.2);
        System.out.println(1.0 / 3 + " " + 2.0 / 3 + " " + 10.0 / 4);
        System.out.println(100.0 + " " + 1e7 + " " + 1.0e-3 + " " + 9999999.0 + " " + 0.001 + " " + 0.0001);
        System.out.println(1e21 + " " + 1.5e300 * 1e10 + " " + -1.0 / 0 + " " + 0.0 / 0);
        System.out.println(Double.MAX_VALUE + " " + Double.MIN_VALUE + " " + Double.MIN_NORMAL);
        System.out.println(-0.0 + " " + (0.0 == -0.0) + " " + Double.compare(0.0, -0.0));
        System.out.println(Math.PI + " " + Math.E + " " + Math.sqrt(2));
        System.out.println(3.14f + " " + 0.1f + " " + (0.1f + 0.2f) + " " + 1.0f / 3 + " " + 1e10f + " " + Float.MAX_VALUE);
        System.out.println((double) 0.1f);
        System.out.println(123456789.0 + " " + 1234567.0 + " " + 12345678.9 + " " + 2e-7);
        System.out.println((int) 3.99 + " " + (int) -3.99 + " " + (int) 1e20 + " " + (int) Double.NaN + " " + (long) -1e30);
        System.out.println((char) 65 + " " + (byte) 200 + " " + (short) 70000 + " " + (int) 'z');
        System.out.println(Math.round(2.5) + " " + Math.round(-2.5) + " " + Math.round(0.49999999999999994) + " " + Math.round(2.4f));
        System.out.println(Math.floor(-1.5) + " " + Math.ceil(-1.5) + " " + Math.rint(2.5) + " " + Math.abs(-0.0));
        System.out.println(Math.pow(2, 10) + " " + Math.max(3, 7) + " " + Math.min(-1.5, 2) + " " + Math.hypot(3, 4));
        System.out.println(Math.floorDiv(-7, 2) + " " + Math.floorMod(-7, 2) + " " + 7 % 3.5 + " " + -7.5 % 2);
        System.out.println(Double.parseDouble("3.25") + " " + Double.parseDouble("1e-5") + " " + Double.valueOf("  42  "));
        System.out.println(Double.parseDouble("0.30000000000000004") + " " + Double.parseDouble("2.2250738585072014E-308"));
        System.out.println(Integer.parseInt("-123") + " " + Integer.parseInt("ff", 16) + " " + Integer.valueOf(77));
        System.out.println(Integer.toHexString(-1) + " " + Integer.toBinaryString(42) + " " + Integer.toOctalString(8));
        System.out.println(Integer.toString(255, 2) + " " + Integer.bitCount(255) + " " + Integer.reverse(1));
        System.out.println(Integer.MAX_VALUE + Integer.MAX_VALUE);
        System.out.println(Integer.compare(3, 5) + " " + Long.compare(5, 3) + " " + Character.getNumericValue('7'));
        try {
            Integer.parseInt("12a");
        } catch (NumberFormatException e) {
            System.out.println("NFE: " + e.getMessage());
        }
        try {
            System.out.println(1 / (args.length));
        } catch (ArithmeticException e) {
            System.out.println("AE: " + e.getMessage());
        }
        // String.format
        System.out.println(String.format("%d|%5d|%-5d|%05d|%,d|%+d|%x|%X|%o", 42, 42, 42, 42, 1234567, 5, 255, 255, 8));
        System.out.println(String.format("%.2f|%10.3f|%-8.1f|%e|%.3e|%g", 3.14159, 2.71828, 1.05, 12345.678, 0.000123, 0.0001234));
        System.out.println(String.format("%.2f %.2f %.1f %.0f %.0f", 0.125, 1.005, 0.05, 0.5, 1.5));
        System.out.println(String.format("%s|%10s|%-10s|%c|%b|%%|%n", "hi", "right", "left", 'x', true).trim());
        System.out.println(String.format("%,.2f|%08.2f|%(d|% d", 1234567.891, -3.5, -42, 42));
        System.out.printf("%s is %d years and %.1f m%n", "Ann", 30, 1.68);
        // arrays of primitives
        long[] la = new long[3];
        la[1] = 1L << 40;
        double[] da = { 1.5, -2.25 };
        boolean[] ba = new boolean[2];
        char[] ca = { 'h', 'i' };
        System.out.println(la[1] + " " + da[0] * da[1] + " " + ba[1] + " " + new String(ca) + " " + ca.length);
        int[][] grid = new int[3][4];
        grid[2][3] = 7;
        int[][][] cube = new int[2][3][4];
        cube[1][2][3] = 9;
        System.out.println(grid.length + " " + grid[0].length + " " + grid[2][3] + " " + cube[1][2][3]);
        int[][] jag = { { 1 }, { 2, 3 }, { 4, 5, 6 } };
        int total = 0;
        for (int[] row : jag) for (int v : row) total += v;
        System.out.println("jagged total " + total);
        // compound ops and increments
        int x = 5;
        x += 3.7;
        byte b = 10;
        b *= 30;
        char c = 'a';
        c += 2;
        System.out.println(x + " " + b + " " + c + " " + (x++ + ++x) + " " + x);
        // bitwise
        System.out.println((0b1010 & 0b0110) + " " + (0b1010 | 0b0110) + " " + (0b1010 ^ 0b0110) + " " + (~5) + " " + 0x7fff_ffff);
        System.out.println(Long.numberOfTrailingZeros(64) + " " + Integer.numberOfLeadingZeros(1) + " " + Integer.highestOneBit(100));
    }
}
