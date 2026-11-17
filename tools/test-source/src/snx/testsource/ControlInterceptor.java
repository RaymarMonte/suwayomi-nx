package snx.testsource;

import java.io.IOException;
import java.io.InterruptedIOException;
import okhttp3.HttpUrl;
import okhttp3.Interceptor;
import okhttp3.Response;

/**
 * Sits in front of the source's HTTP client (before OkHttp's cache, so a cached answer is still checked) and applies the
 * switch file (Control) to every request that goes to our test site: catalog calls, covers and page images.
 */
final class ControlInterceptor implements Interceptor {
    private final CatalogSource source;

    ControlInterceptor(CatalogSource source) {
        this.source = source;
    }

    @Override
    public Response intercept(Interceptor.Chain chain) throws IOException {
        String call = callOf(chain.request().url());
        if (call != null) {
            Control control = source.control();
            if (control.delayMs > 0) {
                try {
                    Thread.sleep(control.delayMs);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new InterruptedIOException("SNX test source: interrupted while waiting");
                }
            }
            if (control.fails(source.getLang(), call)) throw new IOException("SNX test failure: " + call);
        }
        return chain.proceed(chain.request());
    }

    /** Which kind of call a request is: the catalog's "call" parameter, "image" for pages, "cover" for covers; null = not ours. */
    static String callOf(HttpUrl url) {
        if (!url.host().equals("127.0.0.1") || url.port() != 4569) return null;
        String path = url.encodedPath();
        if (path.startsWith("/site/pages/")) return "image";
        if (path.startsWith("/site/covers/")) return "cover";
        if (path.startsWith("/site/catalog-")) return url.queryParameter("call");
        return null;
    }
}
