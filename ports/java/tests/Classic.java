import java.util.*;

/* compiled with javac --release 8: StringBuilder concatenation, anonymous
 * classes, iterators, interfaces, a small bank simulation */
public class Classic {
    interface Account {
        void deposit(double amount);

        boolean withdraw(double amount);

        double balance();
    }

    static class Savings implements Account {
        private double money;
        private final String owner;
        private final List<String> log = new ArrayList<String>();

        Savings(String owner, double initial) {
            this.owner = owner;
            this.money = initial;
            log.add("open " + initial);
        }

        public void deposit(double amount) {
            if (amount <= 0) throw new IllegalArgumentException("amount must be positive");
            money += amount;
            log.add("deposit " + amount);
        }

        public boolean withdraw(double amount) {
            if (amount > money) {
                log.add("refused " + amount);
                return false;
            }
            money -= amount;
            log.add("withdraw " + amount);
            return true;
        }

        public double balance() { return money; }

        public String toString() { return owner + ": " + money + " " + log; }
    }

    static int[] sieve(int n) {
        boolean[] composite = new boolean[n + 1];
        List<Integer> primes = new ArrayList<Integer>();
        for (int i = 2; i <= n; i++) {
            if (composite[i]) continue;
            primes.add(i);
            for (long j = (long) i * i; j <= n; j += i) composite[(int) j] = true;
        }
        int[] r = new int[primes.size()];
        for (int i = 0; i < r.length; i++) r[i] = primes.get(i);
        return r;
    }

    static void hanoi(int n, char from, char to, char via, StringBuilder out) {
        if (n == 0) return;
        hanoi(n - 1, from, via, to, out);
        out.append(from).append(to).append(' ');
        hanoi(n - 1, via, to, from, out);
    }

    public static void main(String[] args) {
        Savings acc = new Savings("ann", 100);
        acc.deposit(50.5);
        System.out.println(acc.withdraw(500) + " " + acc.withdraw(20.25));
        try {
            acc.deposit(-1);
        } catch (IllegalArgumentException e) {
            System.out.println("error: " + e.getMessage());
        }
        System.out.println(acc);
        int[] primes = sieve(100);
        System.out.println(primes.length + " primes, last " + primes[primes.length - 1] + " " + Arrays.toString(Arrays.copyOfRange(primes, 0, 10)));
        StringBuilder moves = new StringBuilder();
        hanoi(4, 'A', 'C', 'B', moves);
        System.out.println(moves.toString().trim());
        List<String> names = new ArrayList<String>(Arrays.asList("delta", "alpha", "charlie", "bravo"));
        Collections.sort(names, new Comparator<String>() {
            public int compare(String a, String b) { return b.compareTo(a); }
        });
        System.out.println(names);
        Map<Character, Integer> freq = new TreeMap<Character, Integer>();
        for (char c : "abracadabra".toCharArray()) {
            Integer old = freq.get(c);
            freq.put(c, old == null ? 1 : old + 1);
        }
        System.out.println(freq);
        long start = System.currentTimeMillis();
        double x = 0;
        for (int i = 1; i <= 100000; i++) x += 1.0 / ((double) i * i);
        System.out.println("pi approx " + Math.sqrt(6 * x) + " elapsed>=0: " + (System.currentTimeMillis() - start >= 0));
        Object[] things = { 1, "two", 3.0, 'c', null, new int[0] };
        for (Object o : things) System.out.print((o == null ? "null" : o.getClass().getSimpleName()) + " ");
        System.out.println();
        StringBuilder tri = new StringBuilder();
        for (int i = 1; i <= 4; i++) {
            for (int j = 0; j < 4 - i; j++) tri.append(' ');
            for (int j = 0; j < 2 * i - 1; j++) tri.append('*');
            tri.append('\n');
        }
        System.out.print(tri);
        System.out.println(Integer.MAX_VALUE + 1L + " " + (char) ('a' + 1) + " " + 7 / 2.0 + " " + (5 & 3) + " " + ("" + 1 + 1));
    }
}
