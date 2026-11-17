package snx.testsource;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * The switch file, site/control.json, which tests write to make calls fail or slow down (tests/e2e/lib/test-source.ts).
 * No file = everything normal. Example:
 *   {"fail": [{"lang": "ja", "call": "search"}], "delayMs": 0, "extraChapters": ["en-01"]}
 * fail: a call of that language (en, ja, fr or *) and kind (popular, latest, search, details, chapters, pages, image, cover
 * or *) throws an IOException "SNX test failure: <call>", so the server answers an error. delayMs (at most 10 000) waits before
 * every call. extraChapters: manga keys whose held-back chapter shows (a "new chapter" for library updates and downloads).
 */
final class Control {
    static final String URL = CatalogSource.SITE + "/control.json";
    static final long MAX_DELAY_MS = 10_000;
    static final Control NORMAL = new Control(Collections.emptyList(), 0, Collections.emptySet());

    private final List<String[]> fail; // {lang, call}
    final long delayMs;
    final Set<String> extraChapters;

    private Control(List<String[]> fail, long delayMs, Set<String> extraChapters) {
        this.fail = fail;
        this.delayMs = delayMs;
        this.extraChapters = extraChapters;
    }

    /** Reads the switch file now. A missing file is "everything normal"; a broken one is an error that says so. */
    static Control read(OkHttpClient client) throws IOException {
        Request request = new Request.Builder().url(URL).header("Cache-Control", "no-cache").build();
        try (Response response = client.newCall(request).execute()) {
            if (response.code() == 404) return NORMAL;
            if (!response.isSuccessful()) throw new IOException("SNX test source: " + URL + " answered HTTP " + response.code());
            return parse(response.body().string());
        }
    }

    static Control parse(String text) throws IOException {
        try {
            JSONObject json = new JSONObject(text);
            List<String[]> fail = new ArrayList<>();
            JSONArray failList = json.optJSONArray("fail");
            if (failList != null) {
                for (int i = 0; i < failList.length(); i++) {
                    JSONObject f = failList.getJSONObject(i);
                    fail.add(new String[] { f.optString("lang", "*"), f.optString("call", "*") });
                }
            }
            long delay = Math.max(0, Math.min(MAX_DELAY_MS, json.optLong("delayMs", 0)));
            Set<String> extra = new HashSet<>();
            JSONArray extraList = json.optJSONArray("extraChapters");
            if (extraList != null) extra.addAll(CatalogSource.strings(extraList));
            return new Control(fail, delay, extra);
        } catch (JSONException e) {
            throw new IOException("SNX test source: " + URL + " is not valid JSON: " + e.getMessage(), e);
        }
    }

    /** True when the switch file says this call of this language must fail. */
    boolean fails(String lang, String call) {
        for (String[] f : fail) {
            if ((f[0].equals("*") || f[0].equals(lang)) && (f[1].equals("*") || f[1].equals(call))) return true;
        }
        return false;
    }
}
