package app.util;

public class Greeter {
    private final String from;

    public Greeter(String from) { this.from = from; }

    public String greet(String who) { return "Hello " + who + " from a " + from + "!"; }
}
