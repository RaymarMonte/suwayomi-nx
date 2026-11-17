package snx.testsource.extra;

import eu.kanade.tachiyomi.source.Source;
import eu.kanade.tachiyomi.source.SourceFactory;
import java.util.Collections;
import java.util.List;
import snx.testsource.CatalogSource;

/** The SNX Extra extension (package snx.testsource.extra): one French source (5 manga), not installed by the seed. */
public class Main implements SourceFactory {
    @Override
    public List<Source> createSources() {
        return Collections.singletonList(new CatalogSource("SNX Extra", "fr", "fr"));
    }
}
