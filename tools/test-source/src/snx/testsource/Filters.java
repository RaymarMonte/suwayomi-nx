package snx.testsource;

import eu.kanade.tachiyomi.source.model.Filter;
import eu.kanade.tachiyomi.source.model.FilterList;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import okhttp3.HttpUrl;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The filter list (the same in every source, positions fixed so tests can address them, see tools/gen-fixtures/test-source.ts)
 * and the rules that pick and order manga. tools/gen-fixtures/test-source.ts has the same rules in TypeScript (selectManga),
 * so tests can predict every answer: change both together.
 *
 *   0 Header "Filters also work without a search text"   1 Text "Author" (contains, any case)
 *   2 Select "Status" (Any, Ongoing, Completed, On hiatus) 3 Separator
 *   4 CheckBox "In colour"                                 5 Group "Genres" of TriStates (include = must have, exclude = must not)
 *   6 Group "Type" of CheckBoxes (none = all, else any)    7 Sort "Sort by" (Popularity, Title, Latest chapter), default Popularity, descending
 * All of them AND together with the search text (title contains, any case).
 */
final class Filters {
    static final String[] STATUSES = { "Any", "Ongoing", "Completed", "On hiatus" };
    static final String[] STATUS_VALUES = { null, "ongoing", "completed", "on hiatus" };
    static final String[] GENRES = { "Action", "Comedy", "Drama", "Fantasy", "Romance", "Sci-Fi", "Long Strip" };
    static final String[] TYPES = { "Manga", "Manhwa", "Webtoon" };
    static final String[] SORTS = { "Popularity", "Title", "Latest chapter" };
    static final int SORT_POPULARITY = 0, SORT_TITLE = 1, SORT_LATEST = 2;

    private Filters() {}

    // Java has to subclass the abstract filter types; these are the plain versions.
    static final class AuthorFilter extends Filter.Text { AuthorFilter() { super("Author", ""); } }
    static final class StatusFilter extends Filter.Select<String> { StatusFilter() { super("Status", STATUSES, 0); } }
    static final class ColourFilter extends Filter.CheckBox { ColourFilter() { super("In colour", false); } }
    static final class GenreFilter extends Filter.TriState { GenreFilter(String name) { super(name, Filter.TriState.STATE_IGNORE); } }
    static final class GenresFilter extends Filter.Group<GenreFilter> { GenresFilter(List<GenreFilter> list) { super("Genres", list); } }
    static final class TypeFilter extends Filter.CheckBox { TypeFilter(String name) { super(name, false); } }
    static final class TypesFilter extends Filter.Group<TypeFilter> { TypesFilter(List<TypeFilter> list) { super("Type", list); } }
    static final class SortFilter extends Filter.Sort {
        SortFilter() { super("Sort by", SORTS, new Filter.Sort.Selection(SORT_POPULARITY, false)); }
    }

    /** A fresh filter list with every filter at its default (the server changes the states it is given). */
    static FilterList create() {
        List<GenreFilter> genres = new ArrayList<>();
        for (String g : GENRES) genres.add(new GenreFilter(g));
        List<TypeFilter> types = new ArrayList<>();
        for (String t : TYPES) types.add(new TypeFilter(t));
        List<Filter<?>> list = new ArrayList<>();
        list.add(new Filter.Header("Filters also work without a search text"));
        list.add(new AuthorFilter());
        list.add(new StatusFilter());
        list.add(new Filter.Separator(""));
        list.add(new ColourFilter());
        list.add(new GenresFilter(genres));
        list.add(new TypesFilter(types));
        list.add(new SortFilter());
        return new FilterList(list);
    }

    /** Writes the filters' states into the request's query string (only what differs from the default). */
    static void addToUrl(FilterList filters, HttpUrl.Builder url) {
        if (filters == null) return;
        for (Filter<?> f : filters) {
            if (f instanceof AuthorFilter) {
                String author = ((AuthorFilter) f).getState();
                if (author != null && !author.trim().isEmpty()) url.addQueryParameter("author", author.trim());
            } else if (f instanceof StatusFilter) {
                Integer i = ((StatusFilter) f).getState();
                if (i != null && i > 0 && i < STATUS_VALUES.length) url.addQueryParameter("status", STATUS_VALUES[i]);
            } else if (f instanceof ColourFilter) {
                if (Boolean.TRUE.equals(((ColourFilter) f).getState())) url.addQueryParameter("colour", "1");
            } else if (f instanceof GenresFilter) {
                for (GenreFilter g : ((GenresFilter) f).getState()) {
                    if (g.isIncluded()) url.addQueryParameter("genre", g.getName());
                    if (g.isExcluded()) url.addQueryParameter("notGenre", g.getName());
                }
            } else if (f instanceof TypesFilter) {
                for (TypeFilter t : ((TypesFilter) f).getState()) {
                    if (Boolean.TRUE.equals(t.getState())) url.addQueryParameter("type", t.getName());
                }
            } else if (f instanceof SortFilter) {
                Filter.Sort.Selection s = ((SortFilter) f).getState();
                if (s != null) {
                    url.addQueryParameter("sort", Integer.toString(s.getIndex()));
                    url.addQueryParameter("asc", s.getAscending() ? "1" : "0");
                }
            }
        }
    }

    /** The manga a list call shows, in order. popular = by popularity, latest = by newest chapter, search = text + filters + sort. */
    static List<JSONObject> select(JSONArray all, HttpUrl url) {
        String call = url.queryParameter("call");
        List<JSONObject> list = new ArrayList<>();
        for (int i = 0; i < all.length(); i++) list.add(all.getJSONObject(i));
        if ("popular".equals(call)) return sorted(list, SORT_POPULARITY, false);
        if ("latest".equals(call)) return sorted(list, SORT_LATEST, false);

        String q = lower(url.queryParameter("q"));
        String author = lower(url.queryParameter("author"));
        String status = url.queryParameter("status");
        boolean colour = "1".equals(url.queryParameter("colour"));
        List<String> genres = url.queryParameterValues("genre");
        List<String> notGenres = url.queryParameterValues("notGenre");
        List<String> types = url.queryParameterValues("type");
        List<JSONObject> matches = new ArrayList<>();
        for (JSONObject m : list) {
            if (q != null && !lower(m.getString("title")).contains(q)) continue;
            if (author != null && !lower(m.getString("author")).contains(author)) continue;
            if (status != null && !status.equals(m.getString("status"))) continue;
            if (colour && !m.getBoolean("colour")) continue;
            Set<String> has = new HashSet<>(CatalogSource.strings(m.getJSONArray("genres")));
            if (!has.containsAll(genres)) continue;
            if (notGenres.stream().anyMatch(has::contains)) continue;
            if (!types.isEmpty() && !types.contains(m.getString("type"))) continue;
            matches.add(m);
        }
        int sort = CatalogSource.parseInt(url.queryParameter("sort"), SORT_POPULARITY);
        boolean ascending = "1".equals(url.queryParameter("asc"));
        return sorted(matches, sort, ascending);
    }

    /** Sorted ascending by the sort key (ties by key), then reversed for descending. */
    private static List<JSONObject> sorted(List<JSONObject> list, int sort, boolean ascending) {
        Comparator<JSONObject> byKey = Comparator.comparing(m -> m.getString("key"));
        Comparator<JSONObject> order;
        switch (sort) {
            case SORT_TITLE: order = Comparator.comparing(m -> lower(m.getString("title"))); break;
            case SORT_LATEST: order = Comparator.comparingLong(Filters::latestChapterDate); break;
            default: order = Comparator.comparingInt(m -> -m.getInt("rank")); break; // least popular first = ascending
        }
        List<JSONObject> result = new ArrayList<>(list);
        result.sort(order.thenComparing(byKey));
        if (!ascending) Collections.reverse(result);
        return result;
    }

    /** The newest date among the chapters that always show (held-back ones don't count); 0 without chapters. */
    static long latestChapterDate(JSONObject m) {
        long latest = 0;
        JSONArray chapters = m.getJSONArray("chapters");
        for (int i = 0; i < chapters.length(); i++) {
            JSONObject c = chapters.getJSONObject(i);
            if (!c.optBoolean("heldBack", false)) latest = Math.max(latest, c.getLong("date"));
        }
        return latest;
    }

    private static String lower(String text) {
        return text == null || text.trim().isEmpty() ? null : text.trim().toLowerCase(Locale.ROOT);
    }
}
