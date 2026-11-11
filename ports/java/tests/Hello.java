public class Hello {
    public static void main(String[] args) {
        System.out.println("Hello, MyOS!");
        System.out.println("args: " + args.length);
        for (int i = 0; i < args.length; i++) System.out.println("  " + i + ": " + args[i]);
        if (args.length > 0 && args[0].equals("exit")) {
            System.out.println("exiting with 3");
            System.exit(3);
        }
        System.out.println("done");
    }
}
