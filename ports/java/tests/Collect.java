import java.util.*;

public class Collect {
    public static void main(String[] args) {
        List<Integer> list = new ArrayList<>();
        for (int i = 10; i > 0; i--) list.add(i * 3 % 7);
        System.out.println(list + " size=" + list.size() + " get(2)=" + list.get(2) + " contains 6: " + list.contains(6));
        Collections.sort(list);
        System.out.println("sorted " + list + " max=" + Collections.max(list) + " min=" + Collections.min(list));
        list.remove(Integer.valueOf(6));
        list.remove(0);
        list.add(0, 99);
        list.set(1, -1);
        System.out.println(list + " indexOf(5)=" + list.indexOf(5) + " sub=" + list.subList(2, 5));
        Collections.reverse(list);
        System.out.println("reversed " + list + " freq(5)=" + Collections.frequency(list, 5));
        Iterator<Integer> it = list.iterator();
        while (it.hasNext()) if (it.next() % 2 == 0) it.remove();
        System.out.println("odd only " + list);
        try {
            for (Integer v : list) if (v == 5) list.remove(v);
        } catch (ConcurrentModificationException e) {
            System.out.println("CME caught");
        }

        LinkedList<String> ll = new LinkedList<>(Arrays.asList("b", "c"));
        ll.addFirst("a");
        ll.addLast("d");
        System.out.println(ll + " " + ll.getFirst() + ll.getLast() + " poll=" + ll.poll() + " pop=" + ll.pop() + " " + ll);

        Map<String, Integer> hm = new HashMap<>();
        String[] words = "the quick brown fox jumps over the lazy dog the end".split(" ");
        for (String w : words) hm.put(w, hm.getOrDefault(w, 0) + 1);
        System.out.println(hm);
        System.out.println(hm.get("the") + " " + hm.get("cat") + " " + hm.containsKey("fox") + " " + hm.size());
        Map<Integer, String> nums = new HashMap<>();
        for (int i = 0; i < 20; i++) nums.put(i * 7, "v" + i);
        System.out.println(nums.keySet());
        nums.keySet().removeIf(k -> k % 2 == 0);
        System.out.println(nums.values());

        Map<String, Integer> tm = new TreeMap<>(hm);
        System.out.println(tm + " first=" + ((TreeMap<String, Integer>) tm).firstKey());
        TreeMap<Integer, String> t2 = new TreeMap<>();
        for (int i = 0; i < 100; i++) t2.put((i * 37) % 101, "x" + i);
        for (int i = 0; i < 100; i += 3) t2.remove((i * 37) % 101);
        System.out.println(t2.size() + " " + t2.firstKey() + " " + t2.lastKey() + " floor(50)=" + t2.floorKey(50) + " ceil(50)=" + t2.ceilingKey(50));
        System.out.println(t2.headMap(10) + " " + t2.tailMap(95));

        Map<String, Integer> lhm = new LinkedHashMap<>();
        lhm.put("zebra", 1);
        lhm.put("apple", 2);
        lhm.put("mango", 3);
        lhm.put("zebra", 4);
        for (Map.Entry<String, Integer> e : lhm.entrySet()) System.out.print(e.getKey() + "=" + e.getValue() + " ");
        System.out.println();

        Set<String> hs = new HashSet<>(Arrays.asList(words));
        System.out.println(hs + " " + hs.size() + " " + hs.contains("dog"));
        Set<Integer> ts = new TreeSet<>(Arrays.asList(5, 3, 9, 1, 3));
        System.out.println(ts + " " + ((TreeSet<Integer>) ts).first() + " " + ((TreeSet<Integer>) ts).higher(3));
        Set<Character> lhs = new LinkedHashSet<>();
        for (char ch : "hello world".toCharArray()) lhs.add(ch);
        System.out.println(lhs);

        Deque<Integer> stack = new ArrayDeque<>();
        Queue<Integer> queue = new ArrayDeque<>();
        for (int i = 1; i <= 5; i++) {
            stack.push(i);
            queue.offer(i);
        }
        System.out.println(stack.pop() + " " + stack.peek() + " " + queue.poll() + " " + queue.peek() + " " + stack + " " + queue);
        PriorityQueue<Integer> pq = new PriorityQueue<>();
        for (int v : new int[] { 5, 1, 8, 3, 9, 2 }) pq.add(v);
        System.out.print("pq order:");
        while (!pq.isEmpty()) System.out.print(" " + pq.poll());
        System.out.println();
        PriorityQueue<String> pq2 = new PriorityQueue<>(Comparator.reverseOrder());
        pq2.addAll(Arrays.asList("pear", "apple", "fig"));
        System.out.println(pq2.poll() + " " + pq2);
        Stack<String> st = new Stack<>();
        st.push("x");
        st.push("y");
        System.out.println(st.peek() + " " + st.pop() + " " + st + " " + st.empty());

        int[] arr = { 5, 2, 9, 1, 7 };
        Arrays.sort(arr);
        System.out.println(Arrays.toString(arr) + " idx(7)=" + Arrays.binarySearch(arr, 7));
        Integer[] boxed = { 3, 1, 2 };
        Arrays.sort(boxed, Collections.reverseOrder());
        System.out.println(Arrays.toString(boxed));
        String[] names = { "bob", "Alice", "carol", "dave" };
        Arrays.sort(names, String.CASE_INSENSITIVE_ORDER);
        System.out.println(Arrays.toString(names) + " " + Arrays.asList(names).indexOf("carol"));
        int[] filled = new int[4];
        Arrays.fill(filled, 7);
        int[] copy = Arrays.copyOf(filled, 6);
        System.out.println(Arrays.toString(copy) + " " + Arrays.equals(filled, new int[] { 7, 7, 7, 7 }));
        System.out.println(Arrays.deepToString(new int[][] { { 1, 2 }, { 3 } }) + " " + Arrays.asList(1, 2, 3));
        List<String> fixed = Arrays.asList("q", "w");
        try {
            fixed.add("e");
        } catch (UnsupportedOperationException e) {
            System.out.println("UOE on add");
        }
        List<Integer> big = new ArrayList<>();
        for (int i = 0; i < 1000; i++) big.add(i);
        long sum = 0;
        for (int v : big) sum += v;
        System.out.println("sum " + sum + " " + big.get(999));
        List<int[]> pairs = new ArrayList<>();
        pairs.add(new int[] { 2, 1 });
        pairs.add(new int[] { 1, 2 });
        pairs.sort((a, b) -> a[0] - b[0]);
        System.out.println(pairs.get(0)[1]);
        Collections.shuffle(big, new Random(42));
        System.out.println(big.subList(0, 8));
        Random r = new Random(12345);
        System.out.println(r.nextInt(100) + " " + r.nextInt() + " " + r.nextLong() + " " + r.nextDouble() + " " + r.nextBoolean());
        System.out.println(Objects.equals(null, null) + " " + Objects.hash(1, 2, 3) + " " + Objects.requireNonNullElse(null, "def"));
        System.out.println(List.of(1, 2, 3) + " " + Map.of("k", 1) + " " + Set.of(42) + " " + Collections.nCopies(3, "ab"));
    }
}
