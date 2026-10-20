// =============================================================================
// AndroidHttpClient - tiny Java helper for tcxCurl's Android backend
// =============================================================================
// Driving java.net.HttpURLConnection from raw JNI is doable but extremely
// verbose (every InputStream.read loop and every byte[] / String conversion
// is half a dozen JNI calls). Doing the request body assembly here keeps the
// C++ side to a single JNI hop per request: pass method/url/headers/body in,
// get a Result back out.
//
// No threading concerns inside this class: the C++ caller is already on a
// worker thread (HttpURLConnection forbids the Android main thread). We use
// HttpURLConnection (not HttpsURLConnection explicitly) — it auto-upgrades
// for https:// URLs.
//
// Lives under com.trussc.tcx.curl so the JNI lookup is stable regardless of
// the consuming app's package name.
// =============================================================================
package com.trussc.tcx.curl;

import java.util.Arrays;
import java.util.zip.GZIPInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

public final class AndroidHttpClient {

    /**
     * One-shot HTTP request result. C++ reads these fields directly via JNI
     * (GetFieldID), so the names/types are part of the ABI — don't rename
     * without updating tcxCurl_android.cpp.
     */
    public static final class Result {
        public int    statusCode;   // 0 on transport error
        public int bodyLength;      // valid bytes in body, no final trim/copy
        public byte[] body;         // never null; empty on error
        public int limitKind;       // 1 body, 2 metadata, 3 redirects, 4 redirect policy; reported once by C++
        public String error;        // null on success
    }

    /**
     * @param method        "GET", "POST", "PUT", "DELETE"
     * @param url           full URL (baseUrl + path joined on the C++ side)
     * @param headerNames   parallel array with headerValues; may be null
     * @param headerValues  parallel array with headerNames; may be null
     * @param body          request body bytes; may be null for GET/DELETE
     * @param connectTimeoutMs  socket connect timeout
     * @param readTimeoutMs     read timeout once connected
     */
    // Bound (security): peers control headers/redirects/error text retained or converted here. Overflow rejects the whole response and is counted by JNI.
    static final int METADATA_LIMIT = 64 * 1024;

    // Mirror tcxCurl.h detail::credentialHeaders; shared by all Java redirect filtering.
    static final String[] CREDENTIAL_HEADERS = {
        "Authorization", "Proxy-Authorization", "X-Api-Key", "Api-Key", "X-Auth-Token", "Cookie"
    };
    static boolean asciiWhitespace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
    static String canonicalHeaderName(String name) throws IOException {
        if (name == null) throw new IOException("Invalid HTTP header name");
        int start = 0, end = name.length();
        while (start < end && asciiWhitespace(name.charAt(start))) ++start;
        while (end > start && asciiWhitespace(name.charAt(end - 1))) --end;
        String trimmed = name.substring(start, end);
        if (trimmed.isEmpty()) throw new IOException("Invalid HTTP header name");
        for (int i = 0; i < trimmed.length(); ++i) {
            char c = trimmed.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || "!#$%&'*+-.^_`|~".indexOf(c) >= 0))
                throw new IOException("Invalid HTTP header name");
        }
        return trimmed;
    }
    static boolean isCredentialHeader(String name) {
        for (String candidate : CREDENTIAL_HEADERS) if (candidate.equalsIgnoreCase(name)) return true;
        return false;
    }
    static void validateHeaderValue(String value) throws IOException {
        if (value == null || value.indexOf('\r') >= 0 || value.indexOf('\n') >= 0 || value.indexOf('\0') >= 0)
            throw new IOException("Invalid HTTP header value");
    }
    static int originPort(URL url) { return url.getPort() == -1 ? url.getDefaultPort() : url.getPort(); }

    static String originZone(String host) {
        int scope = host.indexOf('%');
        if (scope < 0) return "";
        // Match curl's raw ZONEID: consume the delimiter (%25 or raw %),
        // but never percent-decode the interface name itself.
        int start = scope + (host.startsWith("%25", scope) ? 3 : 1);
        int end = host.endsWith("]") ? host.length() - 1 : host.length();
        return host.substring(start, end);
    }

    static boolean sameHost(URL a, URL b) throws IOException {
        try {
            // URI.getHost retains %scope for scoped IPv6 literals. The scope
            // identifies an interface and is case-sensitive; the address is not.
            String left = a.toURI().getHost(), right = b.toURI().getHost();
            if (left == null || right == null) return false;
            int l = left.indexOf('%'), r = right.indexOf('%');
            String leftZone = originZone(left), rightZone = originZone(right);
            // Escaped interface names are ambiguous across transports. Drop
            // credentials even when both raw zones contain the same escape.
            return leftZone.indexOf('%') < 0 && rightZone.indexOf('%') < 0 &&
                leftZone.equals(rightZone) &&
                (l < 0 ? left : left.substring(0, l)).equalsIgnoreCase(r < 0 ? right : right.substring(0, r));
        } catch (java.net.URISyntaxException invalid) { throw new IOException("Invalid HTTP origin", invalid); }
    }

    static final class LimitException extends IOException {
        final int kind;
        LimitException(int kind) { super("HTTP response exceeded its memory limit"); this.kind = kind; }
    }

    // Bound (security): a server controls response size. Overflow rejects the whole response and is counted by JNI.
    // Small, capped growth; pass capacity + used length directly to JNI without
    // a final trim. Live body at handoff is at most array capacity + string.
    static int readBody(InputStream stream, Result result, int limit, boolean discard) throws IOException {
        byte[] data = new byte[discard ? 0 : Math.min(8192, limit)];
        byte[] chunk = new byte[8192];
        int length = 0;
        for (int count; (count = stream.read(chunk)) != -1;) {
            if (count > limit - length) throw new LimitException(1);
            int next = length + count;
            if (!discard && next > data.length) {
                int grown = (int)Math.min(limit, Math.max((long)next, (long)data.length * 2));
                data = Arrays.copyOf(data, grown);
            }
            if (!discard) System.arraycopy(chunk, 0, data, length, count);
            length = next;
        }
        if (!discard) { result.body = data; result.bodyLength = length; }
        return length;
    }

    static boolean permitsBody(String method, int status) {
        return !method.equalsIgnoreCase("HEAD") && status >= 200 && status != 204 && status != 304;
    }

    // Count wire input independently of gzip expansion. No length-driven reserve.
    static final class WireInput extends java.io.FilterInputStream {
        long count;
        final int limit;
        WireInput(InputStream input, int limit) { super(input); this.limit = limit; }
        @Override public int read() throws IOException {
            int value = in.read();
            if (value != -1 && ++count > limit) throw new LimitException(1);
            return value;
        }
        @Override public int read(byte[] bytes, int offset, int length) throws IOException {
            int amount = in.read(bytes, offset, (int)Math.min(length, (long)limit - count + 1));
            if (amount > limit - count) throw new LimitException(1);
            if (amount > 0) count += amount;
            return amount;
        }
    }

    static long metadataBytes(String value) {
        if (value == null) return 0;
        if (value.length() > METADATA_LIMIT) return METADATA_LIMIT + 1L;
        long bytes = 0;
        for (int index = 0; index < value.length(); ++index) {
            char code = value.charAt(index);
            if (code <= 0x7f) ++bytes;
            else if (code <= 0x7ff) bytes += 2;
            else if (Character.isHighSurrogate(code) && index + 1 < value.length() &&
                     Character.isLowSurrogate(value.charAt(index + 1))) { bytes += 4; ++index; }
            else bytes += 3;
        }
        return bytes;
    }

    public static Result request(String method, String url,
                                 String[] headerNames, String[] headerValues,
                                 byte[] body, int connectTimeoutMs, int readTimeoutMs,
                                 boolean followRedirects, long responseLimit) {
        final int bodyLimit = (int)Math.min(responseLimit, Integer.MAX_VALUE - 8L);
        Result result = new Result();
        result.body = new byte[0];
        int metadata = 0, decodedTotal = 0, wireTotal = 0;
        try {
            // C++ prepareHeaders validates names/values and logs the shared per-client
            // Warning before JNI. Mirror validation here before any connection, even
            // for headers that redirect filtering would later remove.
            if (headerNames != null) {
                headerNames = headerNames.clone();
                for (int i = 0; i < headerNames.length; ++i)
                    headerNames[i] = canonicalHeaderName(headerNames[i]);
            }
            if (headerValues != null) {
                headerValues = headerValues.clone();
                for (String value : headerValues) validateHeaderValue(value);
            }
            URL target = new URL(url);
            boolean forwardCredentials = true;
            for (int redirect = 0;; ++redirect) {
                HttpURLConnection conn = (HttpURLConnection) target.openConnection();
                try {
                    conn.setRequestMethod(method);
                    conn.setConnectTimeout(connectTimeoutMs);
                    conn.setReadTimeout(readTimeoutMs);
                    conn.setInstanceFollowRedirects(false);
                    conn.setRequestProperty("Accept-Encoding", "identity");
                    if (headerNames != null && headerValues != null) {
                        int n = Math.min(headerNames.length, headerValues.length);
                        for (int i = 0; i < n; ++i) {
                            String name = headerNames[i];
                            if (!forwardCredentials && isCredentialHeader(name)) continue;
                            conn.setRequestProperty(name, headerValues[i]);
                        }
                    }
                    if (body != null && body.length > 0) {
                        conn.setDoOutput(true);
                        conn.setFixedLengthStreamingMode(body.length);
                        try (OutputStream output = conn.getOutputStream()) { output.write(body); }
                    }
                    int status = conn.getResponseCode();
                    if (metadata > METADATA_LIMIT - 2) throw new LimitException(2);
                    metadata += 2; // empty line after each response header block
                    for (int index = 0;; ++index) {
                        String key = conn.getHeaderFieldKey(index);
                        String value = conn.getHeaderField(index);
                        if (key == null && value == null) break;
                        // Count without making a UTF-8 copy; include separators.
                        long bytes = (key == null ? 2L : 4L) + metadataBytes(key) + metadataBytes(value);
                        if (bytes > METADATA_LIMIT - metadata) throw new LimitException(2);
                        metadata += (int)bytes;
                    }
                    String location = conn.getHeaderField("Location");
                    boolean redirecting = followRedirects && location != null &&
                        (status == 301 || status == 302 || status == 303 || status == 307 || status == 308);
                    long declaredLength = -1;
                    String lengthValue = conn.getHeaderField("Content-Length");
                    if (lengthValue != null) {
                        try { declaredLength = Long.parseLong(lengthValue); }
                        catch (NumberFormatException ignored) { /* transport validates framing */ }
                    }
                    if (permitsBody(method, status) && declaredLength > bodyLimit - wireTotal) throw new LimitException(1);
                    InputStream input = permitsBody(method, status)
                        ? (status >= 400 ? conn.getErrorStream() : conn.getInputStream()) : null;
                    if (input != null) {
                        try (WireInput wire = new WireInput(input, bodyLimit - wireTotal)) {
                            String encoding = conn.getHeaderField("Content-Encoding");
                            if (encoding != null && encoding.equalsIgnoreCase("gzip")) {
                                try (InputStream decoded = new GZIPInputStream(wire)) {
                                    decodedTotal += readBody(decoded, result, bodyLimit - decodedTotal, redirecting);
                                }
                            } else { decodedTotal += readBody(wire, result, bodyLimit - decodedTotal, redirecting); }
                            wireTotal += (int)wire.count;
                            if (declaredLength >= 0 && wire.count != declaredLength)
                                throw new IOException("incomplete response");
                        }
                    }
                    if (permitsBody(method, status) && input == null && declaredLength > 0)
                        throw new IOException("incomplete response");
                    if (redirecting) {
                        // Bound (security): peer redirect chains retain HTTP state. Overflow rejects the whole response and is counted by JNI.
                        if (redirect == 5) throw new LimitException(3);
                        URL next;
                        try { next = new URL(target, location); }
                        catch (java.net.MalformedURLException invalid) { throw new LimitException(4); }
                        long targetBytes = metadataBytes(next.toExternalForm());
                        if (targetBytes > METADATA_LIMIT - metadata) throw new LimitException(2);
                        metadata += (int)targetBytes;
                        // Bound (security): servers choose redirect schemes and TLS transitions. Overflow refuses non-HTTP(S) targets or HTTPS downgrades and is counted by JNI.
                        if ((!next.getProtocol().equals("http") && !next.getProtocol().equals("https")) ||
                            (target.getProtocol().equals("https") && next.getProtocol().equals("http")))
                            throw new LimitException(4);
                        if (!next.getProtocol().equals(target.getProtocol()) ||
                            !sameHost(next, target) || originPort(next) != originPort(target))
                            forwardCredentials = false;
                        target = next;
                        if ((status == 303 && !method.equals("HEAD")) || ((status == 301 || status == 302) && method.equals("POST"))) {
                            method = "GET"; body = null;
                        }
                        continue; // finally releases the previous connection
                    }
                    result.statusCode = status; // publish only a complete body
                    return result;
                } finally { conn.disconnect(); }
            }
        } catch (IOException | RuntimeException e) {
            result.statusCode = 0;
            result.bodyLength = 0;
            result.body = new byte[0];
            if (e instanceof LimitException) result.limitKind = ((LimitException)e).kind;
            // Exception messages can contain arbitrary server-controlled text.
            result.error = result.limitKind == 4 ? "HTTP redirect policy refused target" :
                result.limitKind == 3 ? "HTTP redirect limit exceeded" :
                result.limitKind != 0 ? "HTTP response exceeded its memory limit" : "HTTP request failed";
            return result;
        }
    }
}
