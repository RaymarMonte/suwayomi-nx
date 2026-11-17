package snx.testsource;

import eu.kanade.tachiyomi.source.model.FilterList;
import eu.kanade.tachiyomi.source.model.MangasPage;
import eu.kanade.tachiyomi.source.model.Page;
import eu.kanade.tachiyomi.source.model.SChapter;
import eu.kanade.tachiyomi.source.model.SManga;
import eu.kanade.tachiyomi.source.online.HttpSource;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import okhttp3.HttpUrl;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * One source of our test extension (WP-23, D55). Everything it shows comes from a catalog file on the test site
 * (http://127.0.0.1:4569/site/catalog-<id>.json, written by tools/gen-fixtures/gen-test-source.ts). The source does the
 * paging, searching and filtering itself, so the site can stay plain files.
 *
 * How a call travels: the server asks for a request (popularMangaRequest, ...), we build a URL to the catalog file with the
 * call's details in the query string (jwebserver ignores the query string and sends the file); OkHttp fetches it; the server
 * hands the answer to the matching ...Parse method, which reads the details back from the request URL and picks the manga.
 * Every request first passes ControlInterceptor, which reads the switch file (site/control.json) and can fail or delay it.
 */
@SuppressWarnings("deprecation") // the classic request/parse methods: still what the server calls
public class CatalogSource extends HttpSource {
    /** The test site. Loopback only: the Suwayomi test server runs on the same Windows PC. */
    public static final String SITE = "http://127.0.0.1:4569/site";
    static final int PAGE_SIZE = 25;

    private final String name;
    private final String lang;
    private final String catalogId;
    private volatile OkHttpClient client;
    private volatile OkHttpClient plainClient;

    /**
     * @param name      the source's name (part of its id: never change it)
     * @param lang      its language code (also part of its id)
     * @param catalogId which catalog file it reads: catalog-<catalogId>.json
     */
    public CatalogSource(String name, String lang, String catalogId) {
        this.name = name;
        this.lang = lang;
        this.catalogId = catalogId;
    }

    @Override public String getName() { return name; }
    @Override public String getLang() { return lang; }
    @Override public String getBaseUrl() { return SITE; }
    @Override public boolean getSupportsLatest() { return true; }

    /** The server's own HTTP client with our switch-file check in front of everything (also covers and page images). */
    @Override
    public OkHttpClient getClient() {
        OkHttpClient c = client;
        if (c == null) {
            synchronized (this) {
                if (client == null) client = super.getClient().newBuilder().addInterceptor(new ControlInterceptor(this)).build();
                c = client;
            }
        }
        return c;
    }

    /** A client without our interceptor and without a cache: it reads the switch file fresh on every call. */
    OkHttpClient plainClient() {
        OkHttpClient c = plainClient;
        if (c == null) {
            synchronized (this) {
                if (plainClient == null) plainClient = super.getClient().newBuilder().cache(null).build();
                c = plainClient;
            }
        }
        return c;
    }

    /** The switch file, read now (one small request). */
    Control control() throws IOException {
        return Control.read(plainClient());
    }

    // ---- requests: the catalog URL plus what the parse step needs to know ----

    private HttpUrl.Builder catalogUrl(String call) {
        return HttpUrl.get(SITE + "/catalog-" + catalogId + ".json").newBuilder().addQueryParameter("call", call);
    }

    private Request get(HttpUrl.Builder url) {
        return new Request.Builder().url(url.build()).headers(getHeaders()).build();
    }

    @Override
    protected Request popularMangaRequest(int page) {
        return get(catalogUrl("popular").addQueryParameter("page", Integer.toString(page)));
    }

    @Override
    protected Request latestUpdatesRequest(int page) {
        return get(catalogUrl("latest").addQueryParameter("page", Integer.toString(page)));
    }

    @Override
    protected Request searchMangaRequest(int page, String query, FilterList filters) {
        HttpUrl.Builder url = catalogUrl("search").addQueryParameter("page", Integer.toString(page));
        if (query != null && !query.trim().isEmpty()) url.addQueryParameter("q", query.trim());
        Filters.addToUrl(filters, url);
        return get(url);
    }

    @Override
    public Request mangaDetailsRequest(SManga manga) {
        return get(catalogUrl("details").addQueryParameter("manga", mangaKey(manga.getUrl())));
    }

    @Override
    protected Request chapterListRequest(SManga manga) {
        return get(catalogUrl("chapters").addQueryParameter("manga", mangaKey(manga.getUrl())));
    }

    @Override
    protected Request pageListRequest(SChapter chapter) {
        String[] keys = chapterKeys(chapter.getUrl());
        return get(catalogUrl("pages").addQueryParameter("manga", keys[0]).addQueryParameter("chapter", keys[1]));
    }

    @Override
    public FilterList getFilterList() {
        return Filters.create();
    }

    // ---- parsing ----

    @Override
    protected MangasPage popularMangaParse(Response response) {
        return listPage(response);
    }

    @Override
    protected MangasPage latestUpdatesParse(Response response) {
        return listPage(response);
    }

    @Override
    protected MangasPage searchMangaParse(Response response) {
        return listPage(response);
    }

    /** One page (25) of popular, latest or search results. */
    private MangasPage listPage(Response response) {
        HttpUrl url = response.request().url();
        JSONArray all = readCatalog(response).getJSONArray("manga");
        List<JSONObject> matches = Filters.select(all, url);
        int page = Math.max(1, parseInt(url.queryParameter("page"), 1));
        int from = Math.min((page - 1) * PAGE_SIZE, matches.size());
        int to = Math.min(from + PAGE_SIZE, matches.size());
        List<SManga> mangas = new ArrayList<>();
        for (JSONObject m : matches.subList(from, to)) {
            SManga manga = SManga.Companion.create();
            manga.setUrl("/manga/" + m.getString("key"));
            manga.setTitle(m.getString("title"));
            manga.setThumbnail_url(m.isNull("cover") ? null : m.getString("cover"));
            mangas.add(manga);
        }
        return new MangasPage(mangas, to < matches.size());
    }

    @Override
    protected SManga mangaDetailsParse(Response response) {
        JSONObject m = findManga(readCatalog(response), response.request().url().queryParameter("manga"));
        SManga manga = SManga.Companion.create();
        manga.setUrl("/manga/" + m.getString("key"));
        manga.setTitle(m.getString("title"));
        manga.setAuthor(m.getString("author"));
        manga.setArtist(m.getString("artist"));
        manga.setDescription(m.getString("description"));
        // Only the genres: the type ("Webtoon") stays out, because a "Webtoon" tag makes the app open a strip (D49).
        manga.setGenre(String.join(", ", strings(m.getJSONArray("genres"))));
        manga.setStatus(statusCode(m.getString("status")));
        manga.setThumbnail_url(m.isNull("cover") ? null : m.getString("cover"));
        manga.setInitialized(true);
        return manga;
    }

    /** The chapters, newest first (like real sources). A held-back chapter only shows when the switch file names its manga. */
    @Override
    protected List<SChapter> chapterListParse(Response response) {
        JSONObject m = findManga(readCatalog(response), response.request().url().queryParameter("manga"));
        boolean showHeldBack;
        try {
            showHeldBack = control().extraChapters.contains(m.getString("key"));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        JSONArray chapters = m.getJSONArray("chapters");
        List<SChapter> list = new ArrayList<>();
        for (int i = chapters.length() - 1; i >= 0; i--) {
            JSONObject c = chapters.getJSONObject(i);
            if (c.optBoolean("heldBack", false) && !showHeldBack) continue;
            SChapter chapter = SChapter.Companion.create();
            chapter.setUrl("/chapter/" + m.getString("key") + "/" + c.getString("id"));
            chapter.setName(c.getString("name"));
            chapter.setChapter_number((float) c.getDouble("number"));
            chapter.setScanlator(c.isNull("scanlator") ? null : c.getString("scanlator"));
            chapter.setDate_upload(c.getLong("date"));
            list.add(chapter);
        }
        return list;
    }

    @Override
    protected List<Page> pageListParse(Response response) {
        HttpUrl url = response.request().url();
        JSONObject chapter = findChapter(findManga(readCatalog(response), url.queryParameter("manga")), url.queryParameter("chapter"));
        JSONArray pages = chapter.getJSONArray("pages");
        List<Page> list = new ArrayList<>();
        for (int i = 0; i < pages.length(); i++) list.add(new Page(i, "", pages.getString(i), null));
        return list;
    }

    // ---- helpers ----

    private static JSONObject readCatalog(Response response) {
        try {
            return new JSONObject(response.body().string());
        } catch (IOException e) {
            throw new RuntimeException("SNX test source: couldn't read the catalog", e);
        }
    }

    private static JSONObject findManga(JSONObject catalog, String key) {
        JSONArray all = catalog.getJSONArray("manga");
        for (int i = 0; i < all.length(); i++) {
            JSONObject m = all.getJSONObject(i);
            if (m.getString("key").equals(key)) return m;
        }
        throw new IllegalArgumentException("SNX test source: no manga " + key + " in this catalog");
    }

    private static JSONObject findChapter(JSONObject manga, String id) {
        JSONArray chapters = manga.getJSONArray("chapters");
        for (int i = 0; i < chapters.length(); i++) {
            if (chapters.getJSONObject(i).getString("id").equals(id)) return chapters.getJSONObject(i);
        }
        throw new IllegalArgumentException("SNX test source: no chapter " + id + " in " + manga.getString("key"));
    }

    /** "/manga/en-01" -> "en-01". */
    static String mangaKey(String url) {
        return url.substring(url.lastIndexOf('/') + 1);
    }

    /** "/chapter/en-01/2" -> {"en-01", "2"}. */
    static String[] chapterKeys(String url) {
        String[] parts = url.split("/");
        return new String[] { parts[parts.length - 2], parts[parts.length - 1] };
    }

    static List<String> strings(JSONArray array) {
        List<String> list = new ArrayList<>();
        for (int i = 0; i < array.length(); i++) list.add(array.getString(i));
        return list;
    }

    static int parseInt(String text, int fallback) {
        try {
            return text == null ? fallback : Integer.parseInt(text);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static int statusCode(String status) {
        switch (status) {
            case "ongoing": return SManga.ONGOING;
            case "completed": return SManga.COMPLETED;
            case "on hiatus": return SManga.ON_HIATUS;
            default: return SManga.UNKNOWN;
        }
    }
}
