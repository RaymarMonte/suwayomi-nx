package snx.testsource;

import eu.kanade.tachiyomi.source.Source;
import eu.kanade.tachiyomi.source.SourceFactory;
import java.util.Arrays;
import java.util.List;

/**
 * The SNX Test extension (package snx.testsource): two sources, English (60 manga) and Japanese (8). The manifest names this
 * class (".Main"). The names and languages make the source ids (MD5 of "name/lang/1"): never change them.
 */
public class Main implements SourceFactory {
    @Override
    public List<Source> createSources() {
        return Arrays.asList(new CatalogSource("SNX Test", "en", "en"), new CatalogSource("SNX Test", "ja", "ja"));
    }
}
