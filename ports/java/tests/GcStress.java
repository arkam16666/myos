import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public class GcStress {
    static class Node {
        final int value;
        Node next;
        final int[] payload;

        Node(int value, Node next) {
            this.value = value;
            this.next = next;
            this.payload = new int[8];
        }
    }

    public static void main(String[] args) {
        long checksum = 0;
        // 3 million short-lived objects, far more than the heap holds at once
        for (int round = 0; round < 30; round++) {
            Node head = null;
            for (int i = 0; i < 100000; i++) head = new Node(i, i % 1000 == 0 ? null : head);
            for (Node n = head; n != null; n = n.next) checksum += n.value;
        }
        System.out.println("checksum " + checksum);
        // strings and string building
        String keep = "";
        for (int i = 0; i < 200000; i++) {
            String s = "item" + i;
            if (i % 50000 == 0) keep += s + ";";
        }
        System.out.println("kept " + keep);
        // a growing structure that stays alive, while garbage is made around it
        List<int[]> live = new ArrayList<>();
        for (int i = 0; i < 2000; i++) {
            live.add(new int[100]);
            for (int j = 0; j < 50; j++) {
                byte[] junk = new byte[1000];
                junk[j] = (byte) j;
            }
        }
        System.out.println("live arrays " + live.size());
        HashMap<Integer, String> map = new HashMap<>();
        for (int i = 0; i < 100000; i++) {
            map.put(i % 5000, "value-" + i);
        }
        System.out.println("map " + map.size() + " " + map.get(4999));
        // large arrays come and go
        long big = 0;
        for (int i = 0; i < 40; i++) {
            long[] a = new long[200000];
            a[a.length - 1] = i;
            big += a[a.length - 1];
        }
        System.out.println("big " + big);
        Runtime rt = Runtime.getRuntime();
        System.out.println("memory ok: " + (rt.totalMemory() > 0 && rt.freeMemory() >= 0));
    }
}
