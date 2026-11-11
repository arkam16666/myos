/* uses a class MyOS Java doesn't have: it must stop with a clear error, not crash */
public class Unsupported {
    public static void main(String[] args) {
        System.out.println("before");
        Object p = new java.awt.Point(1, 2);
        System.out.println("after " + p);
    }
}
