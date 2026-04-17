// =============================================================================
// curlExample - tcxCurl HTTPS client example
// =============================================================================
// Fetches data from a public JSON API and displays the results.
// Press SPACE to fetch a new random entry.
// =============================================================================

#include <TrussC.h>
#include <tcxCurl.h>

using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public tc::App {
public:
    HttpClient client;
    string title = "Press SPACE to fetch data";
    string body;
    string status = "Ready";
    int entryId = 1;

    void setup() override {
        // JSONPlaceholder: free fake API for testing
        client.setBaseUrl("https://jsonplaceholder.typicode.com");
        fetchEntry();
    }

    void fetchEntry() {
        status = "Fetching...";
        auto res = client.get("/posts/" + to_string(entryId));

        if (res.ok()) {
            auto data = res.json();
            title = data.value("title", "(no title)");
            body = data.value("body", "");
            status = "OK (status " + to_string(res.statusCode) + ")";
        } else {
            title = "Error";
            body = res.error.empty() ? "HTTP " + to_string(res.statusCode) : res.error;
            status = "Failed";
        }
    }

    void draw() override {
        clear(0.1f);

        setColor(0.5f);
        drawBitmapString("tcxCurl Example - HTTPS GET", 20, 24);
        drawBitmapString("Status: " + status, 20, 48);

        setColor(1.0f, 0.85f, 0.4f);
        drawBitmapString("Post #" + to_string(entryId), 20, 80);

        setColor(1.0f);
        drawBitmapString(title, 20, 108);

        setColor(0.7f);
        drawBitmapString(body, 20, 136);

        setColor(0.4f);
        drawBitmapString("[SPACE] Fetch next  [R] Refetch current", 20, getHeight() - 24);
    }

    void keyPressed(int key) override {
        if (key == ' ') {
            entryId = (entryId % 100) + 1;
            fetchEntry();
        } else if (key == 'r' || key == 'R') {
            fetchEntry();
        }
    }
};

int main() {
    WindowSettings settings;
    settings.width = 640;
    settings.height = 320;
    settings.title = "curlExample";

    return TC_RUN_APP(tcApp, settings);
}
