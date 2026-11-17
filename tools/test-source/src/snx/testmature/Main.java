package snx.testmature;

import eu.kanade.tachiyomi.source.Source;
import eu.kanade.tachiyomi.source.SourceFactory;
import java.util.Collections;
import java.util.List;
import snx.testsource.CatalogSource;

/**
 * The SNX Mature extension (package snx.testmature): one English source (3 manga) flagged NSFW in its manifest and the
 * store. Its pages are the same plain synthetic pages as the rest: only the flag differs. Not installed by the seed.
 */
public class Main implements SourceFactory {
    @Override
    public List<Source> createSources() {
        return Collections.singletonList(new CatalogSource("SNX Mature", "en", "mature-en"));
    }
}
