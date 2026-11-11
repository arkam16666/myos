public class Exceptions {
    static class MyException extends Exception {
        final int code;

        MyException(String msg, int code) {
            super(msg);
            this.code = code;
        }
    }

    static int depth = 0;

    static void level3(int x) throws MyException {
        if (x > 2) throw new MyException("too big: " + x, x);
    }

    static void level2(int x) throws MyException { level3(x + 1); }

    static void level1(int x) throws MyException { level2(x + 1); }

    static int withFinally() {
        try {
            return 1;
        } finally {
            System.out.println("finally runs before return");
        }
    }

    static void recurse() {
        depth++;
        recurse();
    }

    public static void main(String[] args) throws Exception {
        try {
            level1(5);
        } catch (MyException e) {
            System.out.println("caught " + e.getMessage() + " code=" + e.code);
            StackTraceElement[] st = e.getStackTrace();
            for (int i = 0; i < 4; i++) System.out.println("  at " + st[i].getMethodName() + " line " + st[i].getLineNumber());
        }
        System.out.println(withFinally());
        try {
            int[] a = new int[3];
            a[5] = 1;
        } catch (ArrayIndexOutOfBoundsException e) {
            System.out.println("AIOOBE: " + e.getMessage());
        }
        try {
            String s = null;
            s.length();
        } catch (NullPointerException e) {
            System.out.println("NPE caught");
        }
        try {
            Object o = "str";
            Integer i = (Integer) o;
        } catch (ClassCastException e) {
            System.out.println("CCE caught");
        }
        try {
            int[] neg = new int[-1];
        } catch (NegativeArraySizeException e) {
            System.out.println("NASE: " + e.getMessage());
        }
        try {
            Object[] objs = new String[1];
            objs[0] = 1;
        } catch (ArrayStoreException e) {
            System.out.println("ASE: " + e.getMessage());
        }
        try {
            "abc".substring(5);
        } catch (StringIndexOutOfBoundsException e) {
            System.out.println("SIOOBE caught");
        }
        try {
            recurse();
        } catch (StackOverflowError e) {
            System.out.println("StackOverflowError caught, depth > 1000: " + (depth > 1000));
        }
        try {
            try {
                throw new IllegalStateException("inner");
            } finally {
                System.out.println("inner finally");
            }
        } catch (RuntimeException e) {
            System.out.println("outer caught " + e);
        }
        try {
            throw new RuntimeException("wrapper", new java.io.IOException("root cause"));
        } catch (RuntimeException e) {
            System.out.println(e.getMessage() + " <- " + e.getCause().getMessage() + " " + (e.getCause() instanceof java.io.IOException));
        }
        int result = 0;
        for (int i = 0; i < 5; i++) {
            try {
                if (i % 2 == 0) throw new Exception("even " + i);
                result += i;
            } catch (Exception e) {
                result += 100;
            } finally {
                result += 1000;
            }
        }
        System.out.println("loop result " + result);
        try (Res r1 = new Res("A"); Res r2 = new Res("B")) {
            System.out.println("using resources");
            throw new RuntimeException("in body");
        } catch (RuntimeException e) {
            System.out.println("caught " + e.getMessage() + ", suppressed " + e.getSuppressed().length);
        }
        Exception chained = new Exception("top", new IllegalArgumentException("bottom"));
        chained.printStackTrace(System.out);
        System.out.println("now an uncaught one:");
        level1(10);
    }

    static class Res implements AutoCloseable {
        final String name;

        Res(String name) {
            this.name = name;
            System.out.println("open " + name);
        }

        public void close() {
            System.out.println("close " + name);
        }
    }
}
