package app;

import app.util.Greeter;

/* packed into app.jar by tests/qemu_langs.py: packages + java -jar */
public class Main {
    public static void main(String[] args) {
        Greeter g = new Greeter("jar");
        System.out.println(g.greet(args.length > 0 ? args[0] : "world"));
        System.out.println("main class " + Main.class.getName() + " in package " + Main.class.getName().substring(0, Main.class.getName().lastIndexOf('.')));
    }
}
