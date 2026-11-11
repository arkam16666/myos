package java.util;

public final class Locale implements java.io.Serializable {
    public static final Locale ROOT = new Locale("", "");
    public static final Locale ENGLISH = new Locale("en", "");
    public static final Locale US = new Locale("en", "US");
    public static final Locale UK = new Locale("en", "GB");
    public static final Locale GERMANY = new Locale("de", "DE");
    public static final Locale FRANCE = new Locale("fr", "FR");
    private final String language, country;

    public Locale(String language, String country) {
        this.language = language;
        this.country = country;
    }

    public Locale(String language) { this(language, ""); }

    public static Locale getDefault() { return US; }

    public static void setDefault(Locale l) {}

    public static Locale of(String language) { return new Locale(language); }

    public static Locale of(String language, String country) { return new Locale(language, country); }

    public static Locale forLanguageTag(String tag) { return new Locale(tag); }

    public String getLanguage() { return language; }

    public String getCountry() { return country; }

    public String toString() { return country.isEmpty() ? language : language + "_" + country; }
}
