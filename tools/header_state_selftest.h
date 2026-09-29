// Fixture for tools/check_header_state.py (its self-test runs on every check).
// Not compiled and not included anywhere. A declaration the scanner must flag
// carries an `expect:` comment listing the keys it must report on that line;
// the scanner must report exactly these, nothing more. Each block below is a
// construct that once hid state from the scanner or that it must keep telling
// apart.
#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace fx {

// --- What is flagged, and what is not
inline int inlineVar = 0;                                   // expect: fx::inlineVar
inline const int inlineConst = 1;                           // expect: fx::inlineConst
constexpr int notFlaggedConstexpr = 2;
inline constexpr int notFlaggedInlineConstexpr = 3;
static int nsStatic = 0;                                    // expect: fx::nsStatic
static const int notFlaggedNsStaticConst = 4;
extern int notFlaggedExtern;
namespace {
int anonVar = 0;                                            // expect: fx::anonVar
}
inline int& counter() { static int n = 0; return n; }      // expect: fx::counter::n
inline int& perThread() { thread_local int t = 0; return t; }  // expect: fx::perThread::t
inline int notFlaggedLocal() { int plain = 0; return plain; }
inline std::vector<int> directInit(3);                      // expect: fx::directInit
inline std::string directInitText("text");                  // expect: fx::directInitText
inline const std::string constDirectInit("text");           // expect: fx::constDirectInit
std::vector<int> notFlaggedFunctionDecl(int count);

struct Holder {
    static inline int member = 0;                           // expect: fx::Holder::member
    static int notFlaggedDeclaredOnly;
    int& get() { static int local = 0; return local; }      // expect: fx::Holder::get::local
    static int& getStatic() { static int s = 0; return s; } // expect: fx::Holder::getStatic::s
};

inline void withLambda() {
    auto f = []() { static int inLambda = 0; return inLambda; };  // expect: fx::withLambda::<lambda>::inLambda
    (void)f;
}

// --- Compound-assignment operators must not hide the bodies after them
struct V {
    V& operator+=(const V&) { return *this; }
    V& operator-=(const V&) { return *this; }
    V& operator*=(float) { return *this; }
    V& operator/=(float) { return *this; }
    V& operator%=(int) { return *this; }
    V& operator&=(int) { return *this; }
    V& operator|=(int) { return *this; }
    V& operator^=(int) { return *this; }
    V& operator<<=(int) { return *this; }
    V& operator>>=(int) { return *this; }
    int& afterCompound() { static int a = 0; return a; }   // expect: fx::V::afterCompound::a
    static inline int memberAfterCompound = 0;              // expect: fx::V::memberAfterCompound
    V& operator=(const V&) = default;
    bool operator==(const V&) const { return true; }
    int& afterAssign() { static int a2 = 0; return a2; }   // expect: fx::V::afterAssign::a2
};
inline V& operator+=(V& a, int) { return a; }
inline int& afterFreeCompound() { static int b = 0; return b; }  // expect: fx::afterFreeCompound::b

// --- Braced default arguments are not bodies
struct Settings { int x = 0; };
inline void bracedDefault(const Settings& s = {}) { static int c = 0; (void)s; }  // expect: fx::bracedDefault::c
inline int& afterBracedDefault() { static int d = 0; return d; }                 // expect: fx::afterBracedDefault::d
inline void typedBracedDefault(int v = int{1}) { static int e = 0; (void)v; }    // expect: fx::typedBracedDefault::e
struct Recorder {
    void start(const Settings& s = {}) { static int f = 0; (void)s; }          // expect: fx::Recorder::start::f
    int& next() { static int g = 0; return g; }                                 // expect: fx::Recorder::next::g
    static inline int memberAfterBracedDefault = 0;                             // expect: fx::Recorder::memberAfterBracedDefault
};

// --- Braced mem-initializers: the body is the brace after a complete one
struct Ctor {
    Ctor() : b{2} { static int h = 0; (void)h; }                 // expect: fx::Ctor::Ctor::h
    Ctor(int) : a(1), b{2} { static int i = 0; (void)i; }        // expect: fx::Ctor::Ctor::i
    Ctor(long) noexcept : a{1}, b(2) { static int j = 0; (void)j; }  // expect: fx::Ctor::Ctor::j
    Ctor(char) : a{1}, b{2} { static int k = 0; (void)k; }       // expect: fx::Ctor::Ctor::k
    int& after() { static int l = 0; return l; }                 // expect: fx::Ctor::after::l
    int a = 0;
    int b = 0;
};
struct OutOfClass { OutOfClass(); int b; };
inline OutOfClass::OutOfClass() : b{2} { static int m = 0; (void)m; }  // expect: fx::OutOfClass::OutOfClass::m
inline int& afterOutOfClass() { static int n2 = 0; return n2; }       // expect: fx::afterOutOfClass::n2

// --- Templates: one instance per module, like an inline variable
template <class T> struct Reg { static T items; static int count; };
template <class T> T Reg<T>::items;                         // expect: fx::Reg::items
template <class T> int Reg<T>::count = 0;                   // expect: fx::Reg::count
template <class T> T zeroOf = T();                          // expect: fx::zeroOf
template <class T> constexpr T notFlaggedConstexprTemplate = T();
template <class T> struct Tmpl { static inline int shared = 0; };  // expect: fx::Tmpl::shared
template <class T> T notFlaggedFunctionTemplate(T v);
template <class T> using NotFlaggedAlias = Reg<T>;

// --- Every declarator counts
inline int multiA = 0, multiB = 1;                          // expect: fx::multiA fx::multiB
static int* multiP = nullptr, *multiQ = nullptr;            // expect: fx::multiP fx::multiQ
static std::pair<int, int> pairA = std::pair<int, int>(1, 2), pairB;  // expect: fx::pairA fx::pairB
inline int (*fnPtrA)(int) = nullptr, (*fnPtrB)(int) = nullptr;       // expect: fx::fnPtrA fx::fnPtrB
inline void multiLocal() {
    static int o = 0, p = 1;                                // expect: fx::multiLocal::o fx::multiLocal::p
    static int q, r;                                        // expect: fx::multiLocal::q fx::multiLocal::r
    static auto fn = std::function<int(int, int)>(), fn2 = fn;  // expect: fx::multiLocal::fn fx::multiLocal::fn2
    (void)o; (void)p; (void)q; (void)r;
}

// --- A friend operator<< must not open a template bracket that hides what follows
struct Streamable {
    friend std::ostream& operator<<(std::ostream& os, const Streamable&) { return os; }
    int& afterShiftLeft() { static int shl = 0; return shl; }             // expect: fx::Streamable::afterShiftLeft::shl
    static inline int memberAfterShiftLeft = 0;                           // expect: fx::Streamable::memberAfterShiftLeft
    friend std::istream& operator>>(std::istream& is, Streamable&) { return is; }
    int& afterShiftRight() { static int shr = 0; return shr; }            // expect: fx::Streamable::afterShiftRight::shr
    static inline int memberAfterShiftRight = 0;                          // expect: fx::Streamable::memberAfterShiftRight
};
inline std::ostream& operator<<(std::ostream& os, const Settings&) { static int freeShl = 0; (void)freeShl; return os; }  // expect: fx::operator::freeShl
inline int& afterFreeShift() { static int afs = 0; return afs; }          // expect: fx::afterFreeShift::afs

// --- Both sides of an #if
#ifdef _WIN32
inline int windowsOnly = 0;                                 // expect: fx::windowsOnly
#else
inline int otherOnly = 0;                                   // expect: fx::otherOnly
#endif

} // namespace fx
