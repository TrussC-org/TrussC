#include <TrussC.h>
#include "internal/tcAsyncDialog.h"
#include "tc/app/internal/tcAppConstruction.h"
#include "tcResourceLoad.h"
#include <algorithm>

namespace trussc::internal {
namespace {
struct Request {
    std::weak_ptr<std::atomic<bool>> token;
    std::weak_ptr<Window> window;
    bool windowOwned = false;
    DialogCompletion callback;
    DialogDismiss dismiss;
    bool finished = false;
    bool alive() const {
        if (finished || !appRunIsAlive(token)) return false;
        auto live = window.lock();
        return !windowOwned || (live && live->isOpen());
    }
    void cancel() {
        finished = true;
        callback = {};
        if (auto close = std::exchange(dismiss, {})) close();
    }
};
std::vector<std::weak_ptr<Request>> requests;
DialogPresenter testPresenter = nullptr;
}

void setDialogPresenterForTests(DialogPresenter presenter) { testPresenter = presenter; }

void requestAsyncDialog(DialogInputs inputs, DialogCompletion callback, DialogPresenter presenter) {
    auto request = std::make_shared<Request>();
    request->token = appRunToken();
    auto owner = currentAppOwner();
    auto* context = owner ? owner->context : currentWindowCtx();
    if (auto* window = context ? context->hostWindow : nullptr) {
        inputs.requestingWindow = WindowRequestAccess::retainHostOwner(*window);
        request->window = inputs.requestingWindow;
        request->windowOwned = true;
    }
    request->callback = appOwnedCallback(std::move(callback));
    std::erase_if(requests, [](const auto& weak) { return weak.expired(); });
    requests.push_back(request);
    ownAppCancellation([weak = std::weak_ptr(request)] {
        if (auto live = weak.lock()) live->cancel();
    });
    if (testPresenter) presenter = testPresenter;
    enqueueResourceLoad([request, inputs = std::move(inputs), presenter] {
        if (!request->alive()) return;
        request->dismiss = presenter(inputs, [request](FileDialogResult result) {
            // Native handlers normally post; macOS quit is the exception below.
            auto deliver = [request, result = std::move(result)] {
                if (!request->alive()) return;
                request->finished = true;
                request->dismiss = {};
                auto callback = std::exchange(request->callback, {});
                if (callback) callback(std::move(result));
            };
#if defined(__APPLE__) && TARGET_OS_OSX
            // No later prelude exists during accepted macOS quit. Deliver the
            // native cancel while its App is alive, preserving EXIT_PROTOCOL.
            if (completingMacDialogsForQuit()) { deliver(); return; }
#endif
            enqueueResourceLoad(std::move(deliver));
        });
    });
}

void cancelWindowAsyncDialogs(Window& window) {
    for (const auto& weak : requests) {
        if (auto live = weak.lock(); live && live->window.lock().get() == &window) live->cancel();
    }
}

void cancelAsyncDialogs() {
    auto pending = std::exchange(requests, {});
    for (const auto& weak : pending) if (auto live = weak.lock()) live->cancel();
}
} // namespace trussc::internal
