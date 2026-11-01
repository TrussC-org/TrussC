#pragma once

// #239 D6: keep framework tree driving and context out of the public surface.
// Dependent requires-expressions turn inaccessible names into false, so this
// checks actual C++ access control rather than a documentation annotation.
namespace {
template<class T> concept Public_context = requires { &T::context; };
static_assert(!Public_context<trussc::Window>, "Window::context must stay internal");
template<class T> concept Public_dispatchMousePressToTree = requires { &T::dispatchMousePressToTree; };
static_assert(!Public_dispatchMousePressToTree<trussc::Window>, "Window::dispatchMousePressToTree must stay internal");
template<class T> concept Public_dispatchMouseReleaseToTree = requires { &T::dispatchMouseReleaseToTree; };
static_assert(!Public_dispatchMouseReleaseToTree<trussc::Window>, "Window::dispatchMouseReleaseToTree must stay internal");
template<class T> concept Public_dispatchMouseMoveToTree = requires { &T::dispatchMouseMoveToTree; };
static_assert(!Public_dispatchMouseMoveToTree<trussc::Window>, "Window::dispatchMouseMoveToTree must stay internal");
template<class T> concept Public_dispatchMouseScrollToTree = requires { &T::dispatchMouseScrollToTree; };
static_assert(!Public_dispatchMouseScrollToTree<trussc::Window>, "Window::dispatchMouseScrollToTree must stay internal");
template<class T> concept Public_dispatchKeyPressToTree = requires { &T::dispatchKeyPressToTree; };
static_assert(!Public_dispatchKeyPressToTree<trussc::Window>, "Window::dispatchKeyPressToTree must stay internal");
template<class T> concept Public_dispatchKeyReleaseToTree = requires { &T::dispatchKeyReleaseToTree; };
static_assert(!Public_dispatchKeyReleaseToTree<trussc::Window>, "Window::dispatchKeyReleaseToTree must stay internal");
template<class T> concept Public_tickTree = requires { &T::tickTree; };
static_assert(!Public_tickTree<trussc::Window>, "Window::tickTree must stay internal");
template<class T> concept Public_drawTreeNow = requires { &T::drawTreeNow; };
static_assert(!Public_drawTreeNow<trussc::Window>, "Window::drawTreeNow must stay internal");
template<class T> concept Public_syncRootSize = requires { &T::syncRootSize; };
static_assert(!Public_syncRootSize<trussc::Window>, "Window::syncRootSize must stay internal");
template<class T> concept PublicSetClearColor = requires(T& window, const trussc::Color& color) {
    window.setClearColor(color);
};
static_assert(PublicSetClearColor<trussc::Window>);
}
