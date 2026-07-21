// AUTO-GENERATED from reference-data.json by luagen.js — DO NOT EDIT.
#include "tcxLua.h"
#include "TrussC.h"

using namespace trussc;
using namespace std;

// The Lua API deliberately keeps binding deprecated C++ compat names
// (colorFromHSB, tcLog*, ...) so existing Lua scripts stay stable — this shim
// layer is exempt from the CI deprecation gate (-Werror=deprecated-declarations).
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

void tcxLua::setTrussCGeneratedBindings(const std::shared_ptr<sol::state>& lua) {
    lua->set_function("operator*", sol::overload(
        [](float s, const trussc::Vec2 & v) { return trussc::operator*(s, v); },
        [](float s, const trussc::Vec3 & v) { return trussc::operator*(s, v); },
        [](int s, const trussc::IVec2 & v) { return trussc::operator*(s, v); },
        [](int s, const trussc::IVec3 & v) { return trussc::operator*(s, v); },
        [](float s, const trussc::Vec4 & v) { return trussc::operator*(s, v); }
    ));
    lua->set_function("deg2rad", [](float deg) { return trussc::deg2rad(deg); });
    lua->set_function("rad2deg", [](float rad) { return trussc::rad2deg(rad); });
    lua->set_function("clamp", [](float value, float min, float max) { return trussc::clamp(value, min, max); });
    lua->set_function("remap", [](float value, float inMin, float inMax, float outMin, float outMax) { return trussc::remap(value, inMin, inMax, outMin, outMax); });
    lua->set_function("sign", [](float value) { return trussc::sign(value); });
    lua->set_function("fract", [](float value) { return trussc::fract(value); });
    lua->set_function("sq", [](float value) { return trussc::sq(value); });
    lua->set_function("dist", sol::overload(
        [](float x1, float y1, float x2, float y2) { return trussc::dist(x1, y1, x2, y2); },
        [](const trussc::Vec2 & a, const trussc::Vec2 & b) { return trussc::dist(a, b); },
        [](const trussc::Vec3 & a, const trussc::Vec3 & b) { return trussc::dist(a, b); }
    ));
    lua->set_function("distSquared", sol::overload(
        [](float x1, float y1, float x2, float y2) { return trussc::distSquared(x1, y1, x2, y2); },
        [](const trussc::Vec2 & a, const trussc::Vec2 & b) { return trussc::distSquared(a, b); },
        [](const trussc::Vec3 & a, const trussc::Vec3 & b) { return trussc::distSquared(a, b); }
    ));
    lua->set_function("wrap", [](float value, float min, float max) { return trussc::wrap(value, min, max); });
    lua->set_function("angleDifference", [](float angle1, float angle2) { return trussc::angleDifference(angle1, angle2); });
    lua->set_function("angleDifferenceDeg", [](float deg1, float deg2) { return trussc::angleDifferenceDeg(deg1, deg2); });
    lua->set_function("random", sol::overload(
        []() { return trussc::random(); },
        [](float max) { return trussc::random(max); },
        [](float min, float max) { return trussc::random(min, max); }
    ));
    lua->set_function("randomInt", sol::overload(
        [](int max) { return trussc::randomInt(max); },
        [](int min, int max) { return trussc::randomInt(min, max); }
    ));
    lua->set_function("randomSeed", [](unsigned int seed) { return trussc::randomSeed(seed); });
    lua->set_function("signedNoise", sol::overload(
        [](float x) { return trussc::signedNoise(x); },
        [](float x, float y) { return trussc::signedNoise(x, y); },
        [](float x, float y, float z) { return trussc::signedNoise(x, y, z); },
        [](float x, float y, float z, float w) { return trussc::signedNoise(x, y, z, w); },
        [](const trussc::Vec2 & v) { return trussc::signedNoise(v); },
        [](const trussc::Vec3 & v) { return trussc::signedNoise(v); },
        [](float x, float y, float z, int seed) { return trussc::signedNoise(x, y, z, seed); }
    ));
    lua->set_function("noise", sol::overload(
        [](float x) { return trussc::noise(x); },
        [](float x, float y) { return trussc::noise(x, y); },
        [](float x, float y, float z) { return trussc::noise(x, y, z); },
        [](float x, float y, float z, float w) { return trussc::noise(x, y, z, w); },
        [](const trussc::Vec2 & v) { return trussc::noise(v); },
        [](const trussc::Vec3 & v) { return trussc::noise(v); },
        [](float x, float y, float z, int seed) { return trussc::noise(x, y, z, seed); }
    ));
    lua->set_function("fbm", sol::overload(
        [](float x, float y) { return trussc::fbm(x, y); },
        [](float x, float y, int octaves) { return trussc::fbm(x, y, octaves); },
        [](float x, float y, int octaves, float lacunarity) { return trussc::fbm(x, y, octaves, lacunarity); },
        [](float x, float y, int octaves, float lacunarity, float gain) { return trussc::fbm(x, y, octaves, lacunarity, gain); },
        [](float x, float y, float z) { return trussc::fbm(x, y, z); },
        [](float x, float y, float z, int octaves) { return trussc::fbm(x, y, z, octaves); },
        [](float x, float y, float z, int octaves, float lacunarity) { return trussc::fbm(x, y, z, octaves, lacunarity); },
        [](float x, float y, float z, int octaves, float lacunarity, float gain) { return trussc::fbm(x, y, z, octaves, lacunarity, gain); }
    ));
    lua->set_function("getMainThreadId", []() { return trussc::getMainThreadId(); });
    lua->set_function("isMainThread", []() { return trussc::isMainThread(); });
    lua->set_function("runOnMainThread", [](std::function<void ()> fn) { return trussc::runOnMainThread(fn); });
    lua->set_function("logLevelToString", [](trussc::LogLevel level) { return trussc::logLevelToString(level); });
    lua->set_function("getLogger", []() -> decltype(auto) { return trussc::getLogger(); });
    lua->set_function("setConsoleLogLevel", [](trussc::LogLevel level) { return trussc::setConsoleLogLevel(level); });
    lua->set_function("setFileLogLevel", [](trussc::LogLevel level) { return trussc::setFileLogLevel(level); });
    lua->set_function("setLogFile", [](const fs::path & path) { return trussc::setLogFile(path); });
    lua->set_function("closeLogFile", []() { return trussc::closeLogFile(); });
    lua->set_function("tcGetLogger", []() -> decltype(auto) { return trussc::tcGetLogger(); });
    lua->set_function("tcSetConsoleLogLevel", [](trussc::LogLevel level) { return trussc::tcSetConsoleLogLevel(level); });
    lua->set_function("tcSetFileLogLevel", [](trussc::LogLevel level) { return trussc::tcSetFileLogLevel(level); });
    lua->set_function("tcSetLogFile", [](const fs::path & path) { return trussc::tcSetLogFile(path); });
    lua->set_function("tcCloseLogFile", []() { return trussc::tcCloseLogFile(); });
    lua->set_function("logAt", sol::overload(
        []() { return trussc::logAt(); },
        [](trussc::LogLevel level) { return trussc::logAt(level); }
    ));
    lua->set_function("logVerbose", sol::overload(
        []() { return trussc::logVerbose(); },
        [](const std::string & module) { return trussc::logVerbose(module); }
    ));
    lua->set_function("logNotice", sol::overload(
        []() { return trussc::logNotice(); },
        [](const std::string & module) { return trussc::logNotice(module); }
    ));
    lua->set_function("logWarning", sol::overload(
        []() { return trussc::logWarning(); },
        [](const std::string & module) { return trussc::logWarning(module); }
    ));
    lua->set_function("logError", sol::overload(
        []() { return trussc::logError(); },
        [](const std::string & module) { return trussc::logError(module); }
    ));
    lua->set_function("logFatal", sol::overload(
        []() { return trussc::logFatal(); },
        [](const std::string & module) { return trussc::logFatal(module); }
    ));
    lua->set_function("tcLog", sol::overload(
        []() { return trussc::tcLog(); },
        [](trussc::LogLevel level) { return trussc::tcLog(level); }
    ));
    lua->set_function("tcLogVerbose", sol::overload(
        []() { return trussc::tcLogVerbose(); },
        [](const std::string & module) { return trussc::tcLogVerbose(module); }
    ));
    lua->set_function("tcLogNotice", sol::overload(
        []() { return trussc::tcLogNotice(); },
        [](const std::string & module) { return trussc::tcLogNotice(module); }
    ));
    lua->set_function("tcLogWarning", sol::overload(
        []() { return trussc::tcLogWarning(); },
        [](const std::string & module) { return trussc::tcLogWarning(module); }
    ));
    lua->set_function("tcLogError", sol::overload(
        []() { return trussc::tcLogError(); },
        [](const std::string & module) { return trussc::tcLogError(module); }
    ));
    lua->set_function("tcLogFatal", sol::overload(
        []() { return trussc::tcLogFatal(); },
        [](const std::string & module) { return trussc::tcLogFatal(module); }
    ));
    lua->set_function("windowFunction", [](trussc::WindowType type, int i, int n) { return trussc::windowFunction(type, i, n); });
    lua->set_function("isPowerOfTwo", [](int n) { return trussc::isPowerOfTwo(n); });
    lua->set_function("nextPowerOfTwo", [](int n) { return trussc::nextPowerOfTwo(n); });
    lua->set_function("toComplex", [](const std::vector<float> & real) { return trussc::toComplex(real); });
    lua->set_function("fftReal", sol::overload(
        [](const std::vector<float> & signal) { return trussc::fftReal(signal); },
        [](const std::vector<float> & signal, trussc::WindowType window) { return trussc::fftReal(signal, window); }
    ));
    lua->set_function("fftMagnitude", [](const std::vector<std::complex<float>> & spectrum) { return trussc::fftMagnitude(spectrum); });
    lua->set_function("fftMagnitudeDb", [](const std::vector<std::complex<float>> & spectrum, float minDb) { return trussc::fftMagnitudeDb(spectrum, minDb); });
    lua->set_function("fftPhase", [](const std::vector<std::complex<float>> & spectrum) { return trussc::fftPhase(spectrum); });
    lua->set_function("fftPower", [](const std::vector<std::complex<float>> & spectrum) { return trussc::fftPower(spectrum); });
    lua->set_function("binToFrequency", [](int bin, int fftSize, int sampleRate) { return trussc::binToFrequency(bin, fftSize, sampleRate); });
    lua->set_function("frequencyToBin", [](float freq, int fftSize, int sampleRate) { return trussc::frequencyToBin(freq, fftSize, sampleRate); });
    lua->set_function("srgbToLinear", [](float x) { return trussc::srgbToLinear(x); });
    lua->set_function("linearToSrgb", [](float x) { return trussc::linearToSrgb(x); });
    lua->set_function("colorFromHSB", sol::overload(
        [](float h, float s, float b) { return trussc::colorFromHSB(h, s, b); },
        [](float h, float s, float b, float a) { return trussc::colorFromHSB(h, s, b, a); }
    ));
    lua->set_function("colorFromOKLCH", sol::overload(
        [](float L, float C, float H) { return trussc::colorFromOKLCH(L, C, H); },
        [](float L, float C, float H, float a) { return trussc::colorFromOKLCH(L, C, H, a); }
    ));
    lua->set_function("colorFromOKLab", sol::overload(
        [](float L, float a_lab, float b_lab) { return trussc::colorFromOKLab(L, a_lab, b_lab); },
        [](float L, float a_lab, float b_lab, float alpha) { return trussc::colorFromOKLab(L, a_lab, b_lab, alpha); }
    ));
    lua->set_function("colorFromLinear", sol::overload(
        [](float r, float g, float b) { return trussc::colorFromLinear(r, g, b); },
        [](float r, float g, float b, float a) { return trussc::colorFromLinear(r, g, b, a); }
    ));
    lua->set_function("bringWindowToFront", []() { return trussc::bringWindowToFront(); });
    lua->set_function("getDisplayScaleFactor", []() { return trussc::getDisplayScaleFactor(); });
    lua->set_function("getWindowPosition", []() { return trussc::getWindowPosition(); });
    lua->set_function("setWindowPosition", [](int x, int y) { return trussc::setWindowPosition(x, y); });
    lua->set_function("setWindowDecorated", [](bool decorated) { return trussc::setWindowDecorated(decorated); });
    lua->set_function("setWindowSizeLogical", [](int width, int height) { return trussc::setWindowSizeLogical(width, height); });
    lua->set_function("getExecutablePath", []() { return trussc::getExecutablePath(); });
    lua->set_function("getExecutableDir", []() { return trussc::getExecutableDir(); });
    lua->set_function("setImmersiveMode", [](bool enabled) { return trussc::setImmersiveMode(enabled); });
    lua->set_function("getImmersiveMode", []() { return trussc::getImmersiveMode(); });
    lua->set_function("setKeepScreenOn", [](bool enabled) { return trussc::setKeepScreenOn(enabled); });
    lua->set_function("getKeepScreenOn", []() { return trussc::getKeepScreenOn(); });
    lua->set_function("captureWindow", [](trussc::Pixels & outPixels) { return trussc::captureWindow(outPixels); });
    lua->set_function("getSystemVolume", []() { return trussc::getSystemVolume(); });
    lua->set_function("setSystemVolume", [](float volume) { return trussc::setSystemVolume(volume); });
    lua->set_function("getSystemBrightness", []() { return trussc::getSystemBrightness(); });
    lua->set_function("setSystemBrightness", [](float brightness) { return trussc::setSystemBrightness(brightness); });
    lua->set_function("getThermalState", []() { return trussc::getThermalState(); });
    lua->set_function("getThermalTemperature", []() { return trussc::getThermalTemperature(); });
    lua->set_function("getBatteryLevel", []() { return trussc::getBatteryLevel(); });
    lua->set_function("isBatteryCharging", []() { return trussc::isBatteryCharging(); });
    lua->set_function("getAccelerometer", []() { return trussc::getAccelerometer(); });
    lua->set_function("getGyroscope", []() { return trussc::getGyroscope(); });
    lua->set_function("getDeviceOrientation", []() { return trussc::getDeviceOrientation(); });
    lua->set_function("getCompassHeading", []() { return trussc::getCompassHeading(); });
    lua->set_function("isProximityClose", []() { return trussc::isProximityClose(); });
    lua->set_function("getLocation", []() { return trussc::getLocation(); });
    lua->set_function("events", []() -> decltype(auto) { return trussc::events(); });
    lua->set_function("loadErrorName", [](trussc::LoadError e) { return trussc::loadErrorName(e); });
    lua->set_function("initAudio", []() { return trussc::initAudio(); });
    lua->set_function("shutdownAudio", []() { return trussc::shutdownAudio(); });
    lua->set_function("getMicInput", []() -> decltype(auto) { return trussc::getMicInput(); });
    lua->set_function("setDataPathRoot", [](const fs::path & path) { return trussc::setDataPathRoot(path); });
    lua->set_function("getDataPathRoot", []() { return trussc::getDataPathRoot(); });
    lua->set_function("getDataPath", [](const fs::path & filename) { return trussc::getDataPath(filename); });
    lua->set_function("setDataPathToResources", []() { return trussc::setDataPathToResources(); });
    lua->set_function("toInt", [](const std::string & str) { return trussc::toInt(str); });
    lua->set_function("toInt64", [](const std::string & str) { return trussc::toInt64(str); });
    lua->set_function("toFloat", [](const std::string & str) { return trussc::toFloat(str); });
    lua->set_function("toDouble", [](const std::string & str) { return trussc::toDouble(str); });
    lua->set_function("toBool", [](const std::string & str) { return trussc::toBool(str); });
    lua->set_function("toBinary", sol::overload(
        [](int value) { return trussc::toBinary(value); },
        [](unsigned int value) { return trussc::toBinary(value); },
        [](char value) { return trussc::toBinary(value); },
        [](unsigned char value) { return trussc::toBinary(value); },
        [](const std::string & value) { return trussc::toBinary(value); }
    ));
    lua->set_function("hexToInt", [](const std::string & hexStr) { return trussc::hexToInt(hexStr); });
    lua->set_function("hexToUInt", [](const std::string & hexStr) { return trussc::hexToUInt(hexStr); });
    lua->set_function("toBase64", sol::overload(
        [](const std::vector<unsigned char> & bytes) { return trussc::toBase64(bytes); },
        [](const std::string & bytes) { return trussc::toBase64(bytes); }
    ));
    lua->set_function("fromBase64", [](const std::string & encoded) { return trussc::fromBase64(encoded); });
    lua->set_function("isStringInString", [](const std::string & haystack, const std::string & needle) { return trussc::isStringInString(haystack, needle); });
    lua->set_function("stringTimesInString", [](const std::string & haystack, const std::string & needle) { return trussc::stringTimesInString(haystack, needle); });
    lua->set_function("splitString", sol::overload(
        [](const std::string & source, const std::string & delimiter) { return trussc::splitString(source, delimiter); },
        [](const std::string & source, const std::string & delimiter, bool ignoreEmpty) { return trussc::splitString(source, delimiter, ignoreEmpty); },
        [](const std::string & source, const std::string & delimiter, bool ignoreEmpty, bool trim) { return trussc::splitString(source, delimiter, ignoreEmpty, trim); }
    ));
    lua->set_function("joinString", [](const std::vector<std::string> & stringElements, const std::string & delimiter) { return trussc::joinString(stringElements, delimiter); });
    lua->set_function("trim", [](const std::string & src) { return trussc::trim(src); });
    lua->set_function("trimFront", [](const std::string & src) { return trussc::trimFront(src); });
    lua->set_function("trimBack", [](const std::string & src) { return trussc::trimBack(src); });
    lua->set_function("toLower", [](const std::string & src) { return trussc::toLower(src); });
    lua->set_function("toUpper", [](const std::string & src) { return trussc::toUpper(src); });
    lua->set_function("beep", sol::overload(
        []() { return trussc::beep(); },
        [](trussc::Beep type) { return trussc::beep(type); },
        [](float frequency) { return trussc::beep(frequency); },
        [](int frequency) { return trussc::beep(frequency); }
    ));
    lua->set_function("setBeepVolume", [](float vol) { return trussc::setBeepVolume(vol); });
    lua->set_function("getBeepVolume", []() { return trussc::getBeepVolume(); });
    lua->set_function("resetElapsedTimeCounter", []() { return trussc::resetElapsedTimeCounter(); });
    lua->set_function("getElapsedTimef", []() { return trussc::getElapsedTimef(); });
    lua->set_function("getElapsedTimeMillis", []() { return trussc::getElapsedTimeMillis(); });
    lua->set_function("getElapsedTimeMicros", []() { return trussc::getElapsedTimeMicros(); });
    lua->set_function("getSystemTimeMillis", []() { return trussc::getSystemTimeMillis(); });
    lua->set_function("getSystemTimeMicros", []() { return trussc::getSystemTimeMicros(); });
    lua->set_function("getUnixTime", []() { return trussc::getUnixTime(); });
    lua->set_function("sleepMillis", [](int millis) { return trussc::sleepMillis(millis); });
    lua->set_function("sleepMicros", [](int micros) { return trussc::sleepMicros(micros); });
    lua->set_function("getTimestampString", sol::overload(
        [](const std::string & timestampFormat) { return trussc::getTimestampString(timestampFormat); },
        []() { return trussc::getTimestampString(); }
    ));
    lua->set_function("getSeconds", []() { return trussc::getSeconds(); });
    lua->set_function("getMinutes", []() { return trussc::getMinutes(); });
    lua->set_function("getHours", []() { return trussc::getHours(); });
    lua->set_function("getYear", []() { return trussc::getYear(); });
    lua->set_function("getMonth", []() { return trussc::getMonth(); });
    lua->set_function("getDay", []() { return trussc::getDay(); });
    lua->set_function("getWeekday", []() { return trussc::getWeekday(); });
    lua->set_function("compressBound", [](std::size_t nbytes, trussc::Codec codec) { return trussc::compressBound(nbytes, codec); });
    lua->set_function("alertDialog", [](const std::string & title, const std::string & message) { return trussc::alertDialog(title, message); });
    lua->set_function("alertDialogAsync", sol::overload(
        [](const std::string & title, const std::string & message) { return trussc::alertDialogAsync(title, message); },
        [](const std::string & title, const std::string & message, std::function<void ()> callback) { return trussc::alertDialogAsync(title, message, callback); }
    ));
    lua->set_function("confirmDialog", [](const std::string & title, const std::string & message) { return trussc::confirmDialog(title, message); });
    lua->set_function("confirmDialogAsync", [](const std::string & title, const std::string & message, std::function<void (bool)> callback) { return trussc::confirmDialogAsync(title, message, callback); });
    lua->set_function("loadDialog", sol::overload(
        []() { return trussc::loadDialog(); },
        [](const std::string & title) { return trussc::loadDialog(title); },
        [](const std::string & title, const std::string & message) { return trussc::loadDialog(title, message); },
        [](const std::string & title, const std::string & message, const fs::path & defaultPath) { return trussc::loadDialog(title, message, defaultPath); },
        [](const std::string & title, const std::string & message, const fs::path & defaultPath, bool folderSelection) { return trussc::loadDialog(title, message, defaultPath, folderSelection); }
    ));
    lua->set_function("loadDialogAsync", [](const std::string & title, const std::string & message, const fs::path & defaultPath, bool folderSelection, std::function<void (const FileDialogResult &)> callback) { return trussc::loadDialogAsync(title, message, defaultPath, folderSelection, callback); });
    lua->set_function("saveDialog", sol::overload(
        []() { return trussc::saveDialog(); },
        [](const std::string & title) { return trussc::saveDialog(title); },
        [](const std::string & title, const std::string & message) { return trussc::saveDialog(title, message); },
        [](const std::string & title, const std::string & message, const fs::path & defaultPath) { return trussc::saveDialog(title, message, defaultPath); },
        [](const std::string & title, const std::string & message, const fs::path & defaultPath, const fs::path & defaultName) { return trussc::saveDialog(title, message, defaultPath, defaultName); }
    ));
    lua->set_function("saveDialogAsync", [](const std::string & title, const std::string & message, const fs::path & defaultPath, const fs::path & defaultName, std::function<void (const FileDialogResult &)> callback) { return trussc::saveDialogAsync(title, message, defaultPath, defaultName, callback); });
    lua->set_function("loadJson", [](const fs::path & path) { return trussc::loadJson(path); });
    lua->set_function("saveJson", sol::overload(
        [](const trussc::Json & j, const fs::path & path) { return trussc::saveJson(j, path); },
        [](const trussc::Json & j, const fs::path & path, int indent) { return trussc::saveJson(j, path, indent); }
    ));
    lua->set_function("parseJson", [](const std::string & str) { return trussc::parseJson(str); });
    lua->set_function("toJsonString", sol::overload(
        [](const trussc::Json & j) { return trussc::toJsonString(j); },
        [](const trussc::Json & j, int indent) { return trussc::toJsonString(j, indent); }
    ));
    lua->set_function("loadXml", [](const fs::path & path) { return trussc::loadXml(path); });
    lua->set_function("parseXml", [](const std::string & str) { return trussc::parseXml(str); });
    lua->set_function("getFileName", [](const fs::path & path) { return trussc::getFileName(path); });
    lua->set_function("getBaseName", [](const fs::path & path) { return trussc::getBaseName(path); });
    lua->set_function("getFileExtension", [](const fs::path & path) { return trussc::getFileExtension(path); });
    lua->set_function("getParentDirectory", [](const fs::path & path) { return trussc::getParentDirectory(path); });
    lua->set_function("joinPath", [](const fs::path & dir, const fs::path & file) { return trussc::joinPath(dir, file); });
    lua->set_function("getAbsolutePath", [](const fs::path & path) { return trussc::getAbsolutePath(path); });
    lua->set_function("fileExists", [](const fs::path & path) { return trussc::fileExists(path); });
    lua->set_function("directoryExists", [](const fs::path & path) { return trussc::directoryExists(path); });
    lua->set_function("createDirectory", [](const fs::path & path) { return trussc::createDirectory(path); });
    lua->set_function("listDirectory", [](const fs::path & path) { return trussc::listDirectory(path); });
    lua->set_function("removeFile", [](const fs::path & path) { return trussc::removeFile(path); });
    lua->set_function("getFileSize", [](const fs::path & path) { return trussc::getFileSize(path); });
    lua->set_function("loadTextFile", [](const fs::path & path) { return trussc::loadTextFile(path); });
    lua->set_function("saveTextFile", [](const fs::path & path, const std::string & content) { return trussc::saveTextFile(path, content); });
    lua->set_function("appendToFile", [](const fs::path & path, const std::string & content) { return trussc::appendToFile(path, content); });
    lua->set_function("getVersion", []() { return trussc::getVersion(); });
    lua->set_function("getGlobalMouseX", []() { return trussc::getGlobalMouseX(); });
    lua->set_function("getGlobalMouseY", []() { return trussc::getGlobalMouseY(); });
    lua->set_function("getGlobalPMouseX", []() { return trussc::getGlobalPMouseX(); });
    lua->set_function("getGlobalPMouseY", []() { return trussc::getGlobalPMouseY(); });
    lua->set_function("getMouseX", []() { return trussc::getMouseX(); });
    lua->set_function("getMouseY", []() { return trussc::getMouseY(); });
    lua->set_function("getMousePos", []() { return trussc::getMousePos(); });
    lua->set_function("getGlobalMousePos", []() { return trussc::getGlobalMousePos(); });
    lua->set_function("pushMatrix", []() { return trussc::pushMatrix(); });
    lua->set_function("popMatrix", []() { return trussc::popMatrix(); });
    lua->set_function("pushStyle", []() { return trussc::pushStyle(); });
    lua->set_function("popStyle", []() { return trussc::popStyle(); });
    lua->set_function("resetStyle", []() { return trussc::resetStyle(); });
    lua->set_function("translate", sol::overload(
        [](trussc::Vec3 pos) { return trussc::translate(pos); },
        [](float x, float y, float z) { return trussc::translate(x, y, z); },
        [](float x, float y) { return trussc::translate(x, y); }
    ));
    lua->set_function("rotate", sol::overload(
        [](float radians) { return trussc::rotate(radians); },
        [](float x, float y, float z) { return trussc::rotate(x, y, z); },
        [](const trussc::Vec3 & euler) { return trussc::rotate(euler); },
        [](const trussc::Quaternion & quat) { return trussc::rotate(quat); }
    ));
    lua->set_function("rotateX", [](float radians) { return trussc::rotateX(radians); });
    lua->set_function("rotateY", [](float radians) { return trussc::rotateY(radians); });
    lua->set_function("rotateZ", [](float radians) { return trussc::rotateZ(radians); });
    lua->set_function("rotateDeg", sol::overload(
        [](float degrees) { return trussc::rotateDeg(degrees); },
        [](float x, float y, float z) { return trussc::rotateDeg(x, y, z); },
        [](const trussc::Vec3 & euler) { return trussc::rotateDeg(euler); }
    ));
    lua->set_function("rotateXDeg", [](float degrees) { return trussc::rotateXDeg(degrees); });
    lua->set_function("rotateYDeg", [](float degrees) { return trussc::rotateYDeg(degrees); });
    lua->set_function("rotateZDeg", [](float degrees) { return trussc::rotateZDeg(degrees); });
    lua->set_function("scale", sol::overload(
        [](float s) { return trussc::scale(s); },
        [](float sx, float sy) { return trussc::scale(sx, sy); },
        [](float sx, float sy, float sz) { return trussc::scale(sx, sy, sz); }
    ));
    lua->set_function("getMatrix", []() { return trussc::getMatrix(); });
    lua->set_function("getCurrentMatrix", []() { return trussc::getCurrentMatrix(); });
    lua->set_function("getScale", []() { return trussc::getScale(); });
    lua->set_function("resetMatrix", []() { return trussc::resetMatrix(); });
    lua->set_function("multMatrix", [](const trussc::Mat4 & mat) { return trussc::multMatrix(mat); });
    lua->set_function("setMatrix", [](const trussc::Mat4 & mat) { return trussc::setMatrix(mat); });
    lua->set_function("loadMatrix", [](const trussc::Mat4 & mat) { return trussc::loadMatrix(mat); });
    lua->set_function("getCurrentScale", []() { return trussc::getCurrentScale(); });
    lua->set_function("setup", []() { return trussc::setup(); });
    lua->set_function("cleanup", []() { return trussc::cleanup(); });
    lua->set_function("getDpiScale", []() { return trussc::getDpiScale(); });
    lua->set_function("getFramebufferWidth", []() { return trussc::getFramebufferWidth(); });
    lua->set_function("getFramebufferHeight", []() { return trussc::getFramebufferHeight(); });
    lua->set_function("clear", sol::overload(
        [](float r, float g, float b) { return trussc::clear(r, g, b); },
        [](float r, float g, float b, float a) { return trussc::clear(r, g, b, a); },
        []() { return trussc::clear(); },
        [](float gray) { return trussc::clear(gray); },
        [](float gray, float a) { return trussc::clear(gray, a); },
        [](const trussc::Color & c) { return trussc::clear(c); }
    ));
    lua->set_function("setColor", sol::overload(
        [](float r, float g, float b) { return trussc::setColor(r, g, b); },
        [](float r, float g, float b, float a) { return trussc::setColor(r, g, b, a); },
        [](float gray) { return trussc::setColor(gray); },
        [](float gray, float a) { return trussc::setColor(gray, a); },
        [](const trussc::Color & c) { return trussc::setColor(c); }
    ));
    lua->set_function("getColor", []() { return trussc::getColor(); });
    lua->set_function("setColorHSB", sol::overload(
        [](float h, float s, float b) { return trussc::setColorHSB(h, s, b); },
        [](float h, float s, float b, float a) { return trussc::setColorHSB(h, s, b, a); }
    ));
    lua->set_function("setColorOKLab", sol::overload(
        [](float L, float a_lab, float b_lab) { return trussc::setColorOKLab(L, a_lab, b_lab); },
        [](float L, float a_lab, float b_lab, float alpha) { return trussc::setColorOKLab(L, a_lab, b_lab, alpha); }
    ));
    lua->set_function("setColorOKLCH", sol::overload(
        [](float L, float C, float H) { return trussc::setColorOKLCH(L, C, H); },
        [](float L, float C, float H, float alpha) { return trussc::setColorOKLCH(L, C, H, alpha); }
    ));
    lua->set_function("fill", []() { return trussc::fill(); });
    lua->set_function("noFill", []() { return trussc::noFill(); });
    lua->set_function("setStrokeWeight", [](float weight) { return trussc::setStrokeWeight(weight); });
    lua->set_function("getStrokeWeight", []() { return trussc::getStrokeWeight(); });
    lua->set_function("setStrokeCap", [](trussc::StrokeCap cap) { return trussc::setStrokeCap(cap); });
    lua->set_function("getStrokeCap", []() { return trussc::getStrokeCap(); });
    lua->set_function("setStrokeJoin", [](trussc::StrokeJoin join) { return trussc::setStrokeJoin(join); });
    lua->set_function("getStrokeJoin", []() { return trussc::getStrokeJoin(); });
    lua->set_function("setPointSize", [](float px) { return trussc::setPointSize(px); });
    lua->set_function("getPointSize", []() { return trussc::getPointSize(); });
    lua->set_function("setPointStyle", [](trussc::PointStyle s) { return trussc::setPointStyle(s); });
    lua->set_function("getPointStyle", []() { return trussc::getPointStyle(); });
    lua->set_function("setScissor", sol::overload(
        [](float x, float y, float w, float h) { return trussc::setScissor(x, y, w, h); },
        [](int x, int y, int w, int h) { return trussc::setScissor(x, y, w, h); }
    ));
    lua->set_function("resetScissor", []() { return trussc::resetScissor(); });
    lua->set_function("pushScissor", [](float x, float y, float w, float h) { return trussc::pushScissor(x, y, w, h); });
    lua->set_function("popScissor", []() { return trussc::popScissor(); });
    lua->set_function("setBlendMode", [](trussc::BlendMode mode) { return trussc::setBlendMode(mode); });
    lua->set_function("getBlendMode", []() { return trussc::getBlendMode(); });
    lua->set_function("resetBlendMode", []() { return trussc::resetBlendMode(); });
    lua->set_function("enable3D", []() { return trussc::enable3D(); });
    lua->set_function("enable3DPerspective", sol::overload(
        []() { return trussc::enable3DPerspective(); },
        [](float fovY) { return trussc::enable3DPerspective(fovY); },
        [](float fovY, float nearZ) { return trussc::enable3DPerspective(fovY, nearZ); },
        [](float fovY, float nearZ, float farZ) { return trussc::enable3DPerspective(fovY, nearZ, farZ); }
    ));
    lua->set_function("setupScreenFov", sol::overload(
        [](float fovDeg) { return trussc::setupScreenFov(fovDeg); },
        [](float fovDeg, float nearDist) { return trussc::setupScreenFov(fovDeg, nearDist); },
        [](float fovDeg, float nearDist, float farDist) { return trussc::setupScreenFov(fovDeg, nearDist, farDist); }
    ));
    lua->set_function("setupScreenPerspective", sol::overload(
        []() { return trussc::setupScreenPerspective(); },
        [](float fovDeg) { return trussc::setupScreenPerspective(fovDeg); },
        [](float fovDeg, float nearDist) { return trussc::setupScreenPerspective(fovDeg, nearDist); },
        [](float fovDeg, float nearDist, float farDist) { return trussc::setupScreenPerspective(fovDeg, nearDist, farDist); }
    ));
    lua->set_function("setupScreenOrtho", []() { return trussc::setupScreenOrtho(); });
    lua->set_function("setDefaultScreenFov", [](float fovDeg) { return trussc::setDefaultScreenFov(fovDeg); });
    lua->set_function("getDefaultScreenFov", []() { return trussc::getDefaultScreenFov(); });
    lua->set_function("setNearClip", [](float nearDist) { return trussc::setNearClip(nearDist); });
    lua->set_function("setFarClip", [](float farDist) { return trussc::setFarClip(farDist); });
    lua->set_function("getNearClip", []() { return trussc::getNearClip(); });
    lua->set_function("getFarClip", []() { return trussc::getFarClip(); });
    lua->set_function("worldToScreen", [](const trussc::Vec3 & worldPos) { return trussc::worldToScreen(worldPos); });
    lua->set_function("screenToWorld", sol::overload(
        [](const trussc::Vec2 & screenPos) { return trussc::screenToWorld(screenPos); },
        [](const trussc::Vec2 & screenPos, float worldZ) { return trussc::screenToWorld(screenPos, worldZ); }
    ));
    lua->set_function("disable3D", []() { return trussc::disable3D(); });
    lua->set_function("drawRect", sol::overload(
        [](trussc::Vec3 pos, trussc::Vec2 size) { return trussc::drawRect(pos, size); },
        [](trussc::Vec3 pos, float w, float h) { return trussc::drawRect(pos, w, h); },
        [](float x, float y, float w, float h) { return trussc::drawRect(x, y, w, h); }
    ));
    lua->set_function("drawRectRounded", sol::overload(
        [](trussc::Vec3 pos, trussc::Vec2 size, float radius) { return trussc::drawRectRounded(pos, size, radius); },
        [](float x, float y, float w, float h, float radius) { return trussc::drawRectRounded(x, y, w, h, radius); }
    ));
    lua->set_function("drawRectSquircle", sol::overload(
        [](trussc::Vec3 pos, trussc::Vec2 size, float radius) { return trussc::drawRectSquircle(pos, size, radius); },
        [](float x, float y, float w, float h, float radius) { return trussc::drawRectSquircle(x, y, w, h, radius); }
    ));
    lua->set_function("drawSuperellipse", sol::overload(
        [](trussc::Vec3 pos, trussc::Vec2 size) { return trussc::drawSuperellipse(pos, size); },
        [](trussc::Vec3 pos, trussc::Vec2 size, float n) { return trussc::drawSuperellipse(pos, size, n); },
        [](float x, float y, float w, float h) { return trussc::drawSuperellipse(x, y, w, h); },
        [](float x, float y, float w, float h, float n) { return trussc::drawSuperellipse(x, y, w, h, n); }
    ));
    lua->set_function("drawCircle", sol::overload(
        [](trussc::Vec3 center, float radius) { return trussc::drawCircle(center, radius); },
        [](float cx, float cy, float radius) { return trussc::drawCircle(cx, cy, radius); }
    ));
    lua->set_function("drawEllipse", sol::overload(
        [](trussc::Vec3 center, trussc::Vec2 radii) { return trussc::drawEllipse(center, radii); },
        [](trussc::Vec3 center, float rx, float ry) { return trussc::drawEllipse(center, rx, ry); },
        [](float cx, float cy, float rx, float ry) { return trussc::drawEllipse(cx, cy, rx, ry); }
    ));
    lua->set_function("drawArc", sol::overload(
        [](trussc::Vec3 center, float radius, float angleBegin, float angleEnd) { return trussc::drawArc(center, radius, angleBegin, angleEnd); },
        [](float x, float y, float radius, float angleBegin, float angleEnd) { return trussc::drawArc(x, y, radius, angleBegin, angleEnd); }
    ));
    lua->set_function("drawBezier", sol::overload(
        [](trussc::Vec3 p0, trussc::Vec3 p1, trussc::Vec3 p2, trussc::Vec3 p3) { return trussc::drawBezier(p0, p1, p2, p3); },
        [](trussc::Vec3 p0, trussc::Vec3 p1, trussc::Vec3 p2) { return trussc::drawBezier(p0, p1, p2); },
        [](const std::vector<Vec3> & controlPoints) { return trussc::drawBezier(controlPoints); }
    ));
    lua->set_function("drawCurve", sol::overload(
        [](trussc::Vec3 p0, trussc::Vec3 p1, trussc::Vec3 p2, trussc::Vec3 p3) { return trussc::drawCurve(p0, p1, p2, p3); },
        [](const std::vector<Vec3> & points) { return trussc::drawCurve(points); },
        [](const std::vector<Vec3> & points, bool closed) { return trussc::drawCurve(points, closed); }
    ));
    lua->set_function("drawLine", sol::overload(
        [](trussc::Vec3 p1, trussc::Vec3 p2) { return trussc::drawLine(p1, p2); },
        [](float x1, float y1, float x2, float y2) { return trussc::drawLine(x1, y1, x2, y2); },
        [](float x1, float y1, float z1, float x2, float y2, float z2) { return trussc::drawLine(x1, y1, z1, x2, y2, z2); }
    ));
    lua->set_function("drawTriangle", sol::overload(
        [](trussc::Vec3 p1, trussc::Vec3 p2, trussc::Vec3 p3) { return trussc::drawTriangle(p1, p2, p3); },
        [](float x1, float y1, float x2, float y2, float x3, float y3) { return trussc::drawTriangle(x1, y1, x2, y2, x3, y3); }
    ));
    lua->set_function("drawPoint", sol::overload(
        [](trussc::Vec3 pos) { return trussc::drawPoint(pos); },
        [](float x, float y) { return trussc::drawPoint(x, y); }
    ));
    lua->set_function("setCurveTolerance", [](float pixels) { return trussc::setCurveTolerance(pixels); });
    lua->set_function("setCurveResolution", [](int n) { return trussc::setCurveResolution(n); });
    lua->set_function("getCurveTolerance", []() { return trussc::getCurveTolerance(); });
    lua->set_function("getCurveResolution", []() { return trussc::getCurveResolution(); });
    lua->set_function("getCurveMode", []() { return trussc::getCurveMode(); });
    lua->set_function("setCircleResolution", [](int res) { return trussc::setCircleResolution(res); });
    lua->set_function("getCircleResolution", []() { return trussc::getCircleResolution(); });
    lua->set_function("isFillEnabled", []() { return trussc::isFillEnabled(); });
    lua->set_function("isStrokeEnabled", []() { return trussc::isStrokeEnabled(); });
    lua->set_function("getFrameCount", []() { return trussc::getFrameCount(); });
    lua->set_function("drawBitmapString", sol::overload(
        [](const std::string & text, trussc::Vec3 pos) { return trussc::drawBitmapString(text, pos); },
        [](const std::string & text, trussc::Vec3 pos, bool screenFixed) { return trussc::drawBitmapString(text, pos, screenFixed); },
        [](const std::string & text, float x, float y) { return trussc::drawBitmapString(text, x, y); },
        [](const std::string & text, float x, float y, bool screenFixed) { return trussc::drawBitmapString(text, x, y, screenFixed); },
        [](const std::string & text, trussc::Vec3 pos, float scale) { return trussc::drawBitmapString(text, pos, scale); },
        [](const std::string & text, float x, float y, float scale) { return trussc::drawBitmapString(text, x, y, scale); },
        [](const std::string & text, trussc::Vec3 pos, trussc::Direction h, trussc::Direction v) { return trussc::drawBitmapString(text, pos, h, v); },
        [](const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return trussc::drawBitmapString(text, x, y, h, v); }
    ));
    lua->set_function("setTextAlign", [](trussc::Direction h, trussc::Direction v) { return trussc::setTextAlign(h, v); });
    lua->set_function("getTextAlignH", []() { return trussc::getTextAlignH(); });
    lua->set_function("getTextAlignV", []() { return trussc::getTextAlignV(); });
    lua->set_function("setBitmapLineHeight", [](float h) { return trussc::setBitmapLineHeight(h); });
    lua->set_function("getBitmapLineHeight", []() { return trussc::getBitmapLineHeight(); });
    lua->set_function("getBitmapFontHeight", []() { return trussc::getBitmapFontHeight(); });
    lua->set_function("getBitmapStringWidth", [](const std::string & text) { return trussc::getBitmapStringWidth(text); });
    lua->set_function("getBitmapStringHeight", [](const std::string & text) { return trussc::getBitmapStringHeight(text); });
    lua->set_function("getBitmapStringBBox", [](const std::string & text) { return trussc::getBitmapStringBBox(text); });
    lua->set_function("drawBitmapStringHighlight", sol::overload(
        [](const std::string & text, float x, float y) { return trussc::drawBitmapStringHighlight(text, x, y); },
        [](const std::string & text, float x, float y, const trussc::Color & background) { return trussc::drawBitmapStringHighlight(text, x, y, background); },
        [](const std::string & text, float x, float y, const trussc::Color & background, const trussc::Color & foreground) { return trussc::drawBitmapStringHighlight(text, x, y, background, foreground); }
    ));
    lua->set_function("setWindowTitle", [](const std::string & title) { return trussc::setWindowTitle(title); });
    lua->set_function("showCursor", []() { return trussc::showCursor(); });
    lua->set_function("hideCursor", []() { return trussc::hideCursor(); });
    lua->set_function("setCursor", [](trussc::Cursor cursor) { return trussc::setCursor(cursor); });
    lua->set_function("getCursor", []() { return trussc::getCursor(); });
    lua->set_function("bindCursorImage", sol::overload(
        [](trussc::Cursor cursor, const trussc::Image & image) { return trussc::bindCursorImage(cursor, image); },
        [](trussc::Cursor cursor, const trussc::Image & image, int hotspotX) { return trussc::bindCursorImage(cursor, image, hotspotX); },
        [](trussc::Cursor cursor, const trussc::Image & image, int hotspotX, int hotspotY) { return trussc::bindCursorImage(cursor, image, hotspotX, hotspotY); }
    ));
    lua->set_function("unbindCursorImage", [](trussc::Cursor cursor) { return trussc::unbindCursorImage(cursor); });
    lua->set_function("setClipboardString", [](const std::string & text) { return trussc::setClipboardString(text); });
    lua->set_function("getClipboardString", []() { return trussc::getClipboardString(); });
    lua->set_function("setWindowSize", [](int width, int height) { return trussc::setWindowSize(width, height); });
    lua->set_function("setFullscreen", [](bool full) { return trussc::setFullscreen(full); });
    lua->set_function("isFullscreen", []() { return trussc::isFullscreen(); });
    lua->set_function("toggleFullscreen", []() { return trussc::toggleFullscreen(); });
    lua->set_function("operator|", [](trussc::Orientation a, trussc::Orientation b) { return trussc::operator|(a, b); });
    lua->set_function("operator&", [](trussc::Orientation a, trussc::Orientation b) { return trussc::operator&(a, b); });
    lua->set_function("setOrientation", [](trussc::Orientation mask) { return trussc::setOrientation(mask); });
    lua->set_function("getWindowWidth", []() { return trussc::getWindowWidth(); });
    lua->set_function("getWindowHeight", []() { return trussc::getWindowHeight(); });
    lua->set_function("getWindowSize", []() { return trussc::getWindowSize(); });
    lua->set_function("getAspectRatio", []() { return trussc::getAspectRatio(); });
    lua->set_function("getElapsedTime", []() { return trussc::getElapsedTime(); });
    lua->set_function("getUpdateCount", []() { return trussc::getUpdateCount(); });
    lua->set_function("getDrawCount", []() { return trussc::getDrawCount(); });
    lua->set_function("getDeltaTime", []() { return trussc::getDeltaTime(); });
    lua->set_function("getFrameRate", []() { return trussc::getFrameRate(); });
    lua->set_function("getSokolMemoryBytes", []() { return trussc::getSokolMemoryBytes(); });
    lua->set_function("getSokolMemoryAllocs", []() { return trussc::getSokolMemoryAllocs(); });
    lua->set_function("releaseSglBuffers", []() { return trussc::releaseSglBuffers(); });
    lua->set_function("isMousePressed", []() { return trussc::isMousePressed(); });
    lua->set_function("getMouseButton", []() { return trussc::getMouseButton(); });
    lua->set_function("isKeyPressed", [](int key) { return trussc::isKeyPressed(key); });
    lua->set_function("isShiftPressed", []() { return trussc::isShiftPressed(); });
    lua->set_function("isControlPressed", []() { return trussc::isControlPressed(); });
    lua->set_function("isAltPressed", []() { return trussc::isAltPressed(); });
    lua->set_function("isSuperPressed", []() { return trussc::isSuperPressed(); });
    lua->set_function("setTouchAsMouse", [](bool enabled) { return trussc::setTouchAsMouse(enabled); });
    lua->set_function("getTouchAsMouse", []() { return trussc::getTouchAsMouse(); });
    lua->set_function("getBackendName", []() { return trussc::getBackendName(); });
    lua->set_function("getMemoryUsage", []() { return trussc::getMemoryUsage(); });
    lua->set_function("getNodeCount", []() { return trussc::getNodeCount(); });
    lua->set_function("getTextureCount", []() { return trussc::getTextureCount(); });
    lua->set_function("getFboCount", []() { return trussc::getFboCount(); });
    lua->set_function("setFps", [](float fps) { return trussc::setFps(fps); });
    lua->set_function("setIndependentFps", [](float updateFps, float drawFps) { return trussc::setIndependentFps(updateFps, drawFps); });
    lua->set_function("getFpsSettings", []() { return trussc::getFpsSettings(); });
    lua->set_function("getFps", []() { return trussc::getFps(); });
    lua->set_function("redraw", sol::overload(
        []() { return trussc::redraw(); },
        [](int count) { return trussc::redraw(count); }
    ));
    lua->set_function("requestExitApp", []() { return trussc::requestExitApp(); });
    lua->set_function("exitApp", []() { return trussc::exitApp(); });
    lua->set_function("grabScreen", [](trussc::Pixels & outPixels) { return trussc::grabScreen(outPixels); });
    lua->set_function("saveScreenshot", [](const std::filesystem::path & path) { return trussc::saveScreenshot(path); });
    lua->set_function("beginShape", []() { return trussc::beginShape(); });
    lua->set_function("endShape", sol::overload(
        []() { return trussc::endShape(); },
        [](bool close) { return trussc::endShape(close); }
    ));
    lua->set_function("beginLines", []() { return trussc::beginLines(); });
    lua->set_function("endLines", []() { return trussc::endLines(); });
    lua->set_function("beginStroke", []() { return trussc::beginStroke(); });
    lua->set_function("endStroke", sol::overload(
        []() { return trussc::endStroke(); },
        [](bool close) { return trussc::endStroke(close); }
    ));
    lua->set_function("vertex", sol::overload(
        [](float x, float y, float z) { return trussc::vertex(x, y, z); },
        [](float x, float y) { return trussc::vertex(x, y); },
        [](const trussc::Vec2 & v) { return trussc::vertex(v); },
        [](const trussc::Vec3 & v) { return trussc::vertex(v); }
    ));
    lua->set_function("appendArc", sol::overload(
        [](float cx, float cy, float radius, float angleBegin, float angleEnd) { return trussc::appendArc(cx, cy, radius, angleBegin, angleEnd); },
        [](const trussc::Vec2 & center, float radius, float angleBegin, float angleEnd) { return trussc::appendArc(center, radius, angleBegin, angleEnd); }
    ));
    lua->set_function("appendSuperellipse", sol::overload(
        [](float x, float y, float w, float h) { return trussc::appendSuperellipse(x, y, w, h); },
        [](float x, float y, float w, float h, float n) { return trussc::appendSuperellipse(x, y, w, h, n); },
        [](const trussc::Vec2 & pos, const trussc::Vec2 & size) { return trussc::appendSuperellipse(pos, size); },
        [](const trussc::Vec2 & pos, const trussc::Vec2 & size, float n) { return trussc::appendSuperellipse(pos, size, n); }
    ));
    lua->set_function("appendCurve", sol::overload(
        [](const std::vector<Vec3> & points) { return trussc::appendCurve(points); },
        [](const std::vector<Vec3> & points, bool closed) { return trussc::appendCurve(points, closed); }
    ));
    lua->set_function("calculateLighting", [](const trussc::Vec3 & worldPos, const trussc::Vec3 & worldNormal, const trussc::Material & material) { return trussc::calculateLighting(worldPos, worldNormal, material); });
    lua->set_function("channelCount", [](trussc::TextureFormat fmt) { return trussc::channelCount(fmt); });
    lua->set_function("bytesPerPixel", [](trussc::TextureFormat fmt) { return trussc::bytesPerPixel(fmt); });
    lua->set_function("isFloatFormat", [](trussc::TextureFormat fmt) { return trussc::isFloatFormat(fmt); });
    lua->set_function("setEnvironment", [](trussc::Environment & env) { return trussc::setEnvironment(env); });
    lua->set_function("clearEnvironment", []() { return trussc::clearEnvironment(); });
    lua->set_function("getEnvironment", []() { return trussc::getEnvironment(); });
    lua->set_function("drawStroke", sol::overload(
        [](float x1, float y1, float x2, float y2) { return trussc::drawStroke(x1, y1, x2, y2); },
        [](const trussc::Vec2 & p1, const trussc::Vec2 & p2) { return trussc::drawStroke(p1, p2); }
    ));
    lua->set_function("systemFontPath", [](const std::string & name) { return trussc::systemFontPath(name); });
    lua->set_function("listSystemFonts", []() { return trussc::listSystemFonts(); });
    lua->set_function("pushShader", [](trussc::Shader & shader) { return trussc::pushShader(shader); });
    lua->set_function("popShader", []() { return trussc::popShader(); });
    lua->set_function("videoCodecName", [](trussc::VideoCodec c) { return trussc::videoCodecName(c); });
    lua->set_function("startRecording", sol::overload(
        [](const fs::path & path) { return trussc::startRecording(path); },
        [](const fs::path & path, const trussc::VideoRecordSettings & settings) { return trussc::startRecording(path, settings); },
        [](const fs::path & path, float durationSec) { return trussc::startRecording(path, durationSec); },
        [](const trussc::Fbo & fbo, const fs::path & path) { return trussc::startRecording(fbo, path); },
        [](const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return trussc::startRecording(fbo, path, settings); },
        [](const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return trussc::startRecording(fbo, path, durationSec); }
    ));
    lua->set_function("stopRecording", []() { return trussc::stopRecording(); });
    lua->set_function("isRecording", []() { return trussc::isRecording(); });
    lua->set_function("recordingFrameCount", []() { return trussc::recordingFrameCount(); });
    lua->set_function("recordingPath", []() { return trussc::recordingPath(); });
    lua->set_function("createPlane", sol::overload(
        [](float width, float height) { return trussc::createPlane(width, height); },
        [](float width, float height, int cols) { return trussc::createPlane(width, height, cols); },
        [](float width, float height, int cols, int rows) { return trussc::createPlane(width, height, cols, rows); }
    ));
    lua->set_function("createBox", sol::overload(
        [](float width, float height, float depth) { return trussc::createBox(width, height, depth); },
        [](float size) { return trussc::createBox(size); }
    ));
    lua->set_function("createSphere", sol::overload(
        [](float radius) { return trussc::createSphere(radius); },
        [](float radius, int resolution) { return trussc::createSphere(radius, resolution); }
    ));
    lua->set_function("createCylinder", sol::overload(
        [](float radius, float height) { return trussc::createCylinder(radius, height); },
        [](float radius, float height, int resolution) { return trussc::createCylinder(radius, height, resolution); }
    ));
    lua->set_function("createCapsule", sol::overload(
        [](float radius, float cylinderHeight) { return trussc::createCapsule(radius, cylinderHeight); },
        [](float radius, float cylinderHeight, int resolution) { return trussc::createCapsule(radius, cylinderHeight, resolution); }
    ));
    lua->set_function("createCone", sol::overload(
        [](float radius, float height) { return trussc::createCone(radius, height); },
        [](float radius, float height, int resolution) { return trussc::createCone(radius, height, resolution); }
    ));
    lua->set_function("createIcoSphere", sol::overload(
        [](float radius) { return trussc::createIcoSphere(radius); },
        [](float radius, int subdivisions) { return trussc::createIcoSphere(radius, subdivisions); }
    ));
    lua->set_function("createTorus", sol::overload(
        [](float radius, float tubeRadius) { return trussc::createTorus(radius, tubeRadius); },
        [](float radius, float tubeRadius, int sides) { return trussc::createTorus(radius, tubeRadius, sides); },
        [](float radius, float tubeRadius, int sides, int rings) { return trussc::createTorus(radius, tubeRadius, sides, rings); }
    ));
    lua->set_function("drawBox", sol::overload(
        [](float w, float h, float d) { return trussc::drawBox(w, h, d); },
        [](float size) { return trussc::drawBox(size); },
        [](trussc::Vec3 pos, float w, float h, float d) { return trussc::drawBox(pos, w, h, d); },
        [](float x, float y, float z, float w, float h, float d) { return trussc::drawBox(x, y, z, w, h, d); },
        [](trussc::Vec3 pos, float size) { return trussc::drawBox(pos, size); },
        [](float x, float y, float z, float size) { return trussc::drawBox(x, y, z, size); }
    ));
    lua->set_function("drawSphere", sol::overload(
        [](float radius) { return trussc::drawSphere(radius); },
        [](float radius, int resolution) { return trussc::drawSphere(radius, resolution); },
        [](trussc::Vec3 pos, float radius) { return trussc::drawSphere(pos, radius); },
        [](trussc::Vec3 pos, float radius, int resolution) { return trussc::drawSphere(pos, radius, resolution); },
        [](float x, float y, float z, float radius) { return trussc::drawSphere(x, y, z, radius); },
        [](float x, float y, float z, float radius, int resolution) { return trussc::drawSphere(x, y, z, radius, resolution); }
    ));
    lua->set_function("drawCone", sol::overload(
        [](float radius, float height) { return trussc::drawCone(radius, height); },
        [](float radius, float height, int resolution) { return trussc::drawCone(radius, height, resolution); },
        [](trussc::Vec3 pos, float radius, float height) { return trussc::drawCone(pos, radius, height); },
        [](trussc::Vec3 pos, float radius, float height, int resolution) { return trussc::drawCone(pos, radius, height, resolution); },
        [](float x, float y, float z, float radius, float height) { return trussc::drawCone(x, y, z, radius, height); },
        [](float x, float y, float z, float radius, float height, int resolution) { return trussc::drawCone(x, y, z, radius, height, resolution); }
    ));
    lua->set_function("addLight", [](trussc::Light & light) { return trussc::addLight(light); });
    lua->set_function("removeLight", [](trussc::Light & light) { return trussc::removeLight(light); });
    lua->set_function("clearLights", []() { return trussc::clearLights(); });
    lua->set_function("getNumLights", []() { return trussc::getNumLights(); });
    lua->set_function("setMaterial", [](trussc::Material & material) { return trussc::setMaterial(material); });
    lua->set_function("clearMaterial", []() { return trussc::clearMaterial(); });
    lua->set_function("beginShadowPass", [](trussc::Light & light) { return trussc::beginShadowPass(light); });
    lua->set_function("endShadowPass", []() { return trussc::endShadowPass(); });
    lua->set_function("shadowDraw", [](const trussc::Mesh & mesh) { return trussc::shadowDraw(mesh); });
    lua->set_function("setCameraPosition", sol::overload(
        [](const trussc::Vec3 & pos) { return trussc::setCameraPosition(pos); },
        [](float x, float y, float z) { return trussc::setCameraPosition(x, y, z); }
    ));
    lua->set_function("getCameraPosition", []() -> decltype(auto) { return trussc::getCameraPosition(); });
    lua->set_function("listNetworkInterfaces", []() { return trussc::listNetworkInterfaces(); });
    lua->set_function("printNetworkInterfaces", []() { return trussc::printNetworkInterfaces(); });
    lua->set_function("getLocalIp", []() { return trussc::getLocalIp(); });
    lua->set_function("getLocalIps", []() { return trussc::getLocalIps(); });
    lua->set_function("isLoopback", [](const std::string & addr) { return trussc::isLoopback(addr); });
    lua->set_function("isPrivate", [](const std::string & addr) { return trussc::isPrivate(addr); });
    lua->set_function("isLinkLocal", [](const std::string & addr) { return trussc::isLinkLocal(addr); });
    lua->set_function("sameSubnet", [](const std::string & a, const std::string & b, const std::string & netmask) { return trussc::sameSubnet(a, b, netmask); });
    lua->set_function("getOui", [](const std::string & mac) { return trussc::getOui(mac); });
    lua->set_function("isLocallyAdministered", [](const std::string & mac) { return trussc::isLocallyAdministered(mac); });
    lua->set_function("easeIn", [](float t, trussc::EaseType type) { return trussc::easeIn(t, type); });
    lua->set_function("easeOut", [](float t, trussc::EaseType type) { return trussc::easeOut(t, type); });
    lua->set_function("easeInOut", sol::overload(
        [](float t, trussc::EaseType type) { return trussc::easeInOut(t, type); },
        [](float t, trussc::EaseType inType, trussc::EaseType outType) { return trussc::easeInOut(t, inType, outType); }
    ));
    lua->set_function("ease", [](float t, trussc::EaseType type, trussc::EaseMode mode) { return trussc::ease(t, type, mode); });
    lua->set_function("typeName", [](const std::type_info & ti) -> decltype(auto) { return trussc::typeName(ti); });
    lua->set_function("shortTypeName", [](const std::type_info & ti) -> decltype(auto) { return trussc::shortTypeName(ti); });
    lua->set_function("isOverlayHovered", []() { return trussc::isOverlayHovered(); });
    lua->set_function("isOverlayFocused", []() { return trussc::isOverlayFocused(); });
    lua->set_function("getSelectedNode", []() { return trussc::getSelectedNode(); });
    lua->set_function("getRootNode", []() { return trussc::getRootNode(); });
    lua->set_function("createWindow", sol::overload(
        []() { return trussc::createWindow(); },
        [](const trussc::WindowSettings & settings) { return trussc::createWindow(settings); }
    ));
    lua->set_function("nodeToJson", [](trussc::Node & node, int maxDepth) { return trussc::nodeToJson(node, maxDepth); });
    lua->set_function("lerp", [](float a, float b, float t) { return std::lerp(a, b, t); });
    lua->set_function("sin", [](float x) { return std::sin(x); });
    lua->set_function("cos", [](float x) { return std::cos(x); });
    lua->set_function("tan", [](float x) { return std::tan(x); });
    lua->set_function("asin", [](float x) { return std::asin(x); });
    lua->set_function("acos", [](float x) { return std::acos(x); });
    lua->set_function("atan", [](float x) { return std::atan(x); });
    lua->set_function("atan2", [](float y, float x) { return std::atan2(y, x); });
    lua->set_function("abs", [](float x) { return std::abs(x); });
    lua->set_function("sqrt", [](float x) { return std::sqrt(x); });
    lua->set_function("pow", [](float x, float y) { return std::pow(x, y); });
    lua->set_function("log", [](float x) { return std::log(x); });
    lua->set_function("exp", [](float x) { return std::exp(x); });
    lua->set_function("min", [](float a, float b) { return std::min(a, b); });
    lua->set_function("max", [](float a, float b) { return std::max(a, b); });
    lua->set_function("floor", [](float x) { return std::floor(x); });
    lua->set_function("ceil", [](float x) { return std::ceil(x); });
    lua->set_function("round", [](float x) { return std::round(x); });
    lua->set_function("fmod", [](float x, float y) { return std::fmod(x, y); });
    lua->set_function("atanh", [](float x) { return std::atanh(x); });
    lua->set_function("tanh", [](float x) { return std::tanh(x); });
    lua->set_function("exp2", [](float x) { return std::exp2(x); });
    lua->set_function("log2", [](float x) { return std::log2(x); });
    lua->set_function("log10", [](float x) { return std::log10(x); });
    lua->set_function("trunc", [](float x) { return std::trunc(x); });
    lua->set_function("remainder", [](float x, float y) { return std::remainder(x, y); });
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
