// Internal, shared native publication transaction. Policy stays at the caller.
#ifndef SOKOL_WINDOW_CREATION_INCLUDED
#define SOKOL_WINDOW_CREATION_INCLUDED
#include <utility>
template<class Window, class Rollback>
class _sapp_tc_creation_transaction {
public:
    _sapp_tc_creation_transaction(Window* window, Rollback rollback)
        : window_(window), rollback_(std::move(rollback)) { window_->constructing = true; }
    ~_sapp_tc_creation_transaction() noexcept {
        if (window_) {
            window_->ready = false;
            window_->constructing = false;
            rollback_(window_);
        }
    }
    _sapp_tc_creation_transaction(const _sapp_tc_creation_transaction&) = delete;
    void commit() noexcept { window_->constructing = false; window_ = nullptr; }
    // A native API may synchronously remove the identity by an independent
    // OS destruction path. Its unwinder has already consumed these resources.
    void removed() noexcept { window_ = nullptr; }
private:
    Window* window_;
    Rollback rollback_;
};
#endif
