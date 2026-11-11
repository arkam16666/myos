import java.util.Scanner;

public class ScanTest {
    public static void main(String[] args) {
        Scanner in = new Scanner(System.in);
        System.out.print("Name? ");
        String name = in.nextLine();
        System.out.print("Two numbers? ");
        int a = in.nextInt();
        int b = in.nextInt();
        in.nextLine();
        System.out.print("A decimal? ");
        double d = in.nextDouble();
        System.out.println("Hello " + name + ", " + a + " + " + b + " = " + (a + b) + ", half of " + d + " is " + d / 2);
        in.nextLine();
        System.out.print("Words? ");
        String line = in.nextLine();
        Scanner words = new Scanner(line);
        int n = 0;
        while (words.hasNext()) {
            String w = words.next();
            n++;
            System.out.println(n + ": " + w);
        }
        System.out.println("bye");
    }
}
