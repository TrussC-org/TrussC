// Shared TC_PLATFORMS conditions for both Lua generators.
const ALL_PLATFORMS = ['macos', 'windows', 'linux', 'android', 'ios', 'web'];
const PLATFORM_COND = {
    macos: '(defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))',
    ios: '(defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)',
    windows: 'defined(_WIN32)',
    linux: '(defined(__linux__) && !defined(__ANDROID__))',
    android: 'defined(__ANDROID__)',
    web: 'defined(__EMSCRIPTEN__)',
};
// -> condition string, or null when unrestricted (no annotation / covers all)
function platformGuard(platforms) {
    if (!platforms || !platforms.length) return null;
    if (ALL_PLATFORMS.every(p => platforms.includes(p))) return null;
    return platforms.map(p => PLATFORM_COND[p]).filter(Boolean).join(' || ');
}

module.exports = { platformGuard };
