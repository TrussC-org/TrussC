#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <bitset>
#include <cstdint>

// tcPlatform.h の前方宣言（循環インクルード回避）
namespace trussc { namespace platform {
    std::string getExecutableDir();
}}

namespace trussc {

// ---------------------------------------------------------------------------
// データパス（oF の ofToDataPath と同様）
// ---------------------------------------------------------------------------

namespace internal {
    // デフォルトは "data/"（実行ファイルからの相対パス）
    inline std::string dataPathRoot = "data/";
    inline bool dataPathRootIsAbsolute = false;
}

// データパスのルートを設定
// 相対パスの場合、実行ファイルのディレクトリを基準に解決される
// 絶対パス（/ で始まる）の場合はそのまま使用
inline void setDataPathRoot(const std::string& path) {
    internal::dataPathRoot = path;
    // 末尾にスラッシュがなければ追加
    if (!internal::dataPathRoot.empty() && internal::dataPathRoot.back() != '/') {
        internal::dataPathRoot += '/';
    }
    // 絶対パスかどうかを記録
    internal::dataPathRootIsAbsolute = (!path.empty() && path[0] == '/');
}

// データパスのルートを取得
inline std::string getDataPathRoot() {
    return internal::dataPathRoot;
}

// ファイル名からデータパスを取得
// 実行ファイルのディレクトリを基準に解決される
inline std::string getDataPath(const std::string& filename) {
    if (internal::dataPathRootIsAbsolute) {
        // 絶対パスの場合はそのまま
        return internal::dataPathRoot + filename;
    } else {
        // 相対パスの場合は実行ファイルのディレクトリを基準に
        return platform::getExecutableDir() + internal::dataPathRoot + filename;
    }
}

// macOS バンドル配布用: Resources フォルダを data パスに設定
// xxx.app/Contents/Resources/data/ を参照するようになる
// macOS 以外では何もしない
inline void setDataPathToResources() {
    #ifdef __APPLE__
    setDataPathRoot("../Resources/data/");
    #endif
}

// ---------------------------------------------------------------------------
// toString - 値を文字列に変換
// ---------------------------------------------------------------------------

// 基本的な文字列変換
template <class T>
std::string toString(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// 小数点精度を指定して文字列に変換
// 例: toString(3.14159, 2) → "3.14"
template <class T>
std::string toString(const T& value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// 幅とフィル文字を指定して文字列に変換
// 例: toString(42, 5, '0') → "00042"
template <class T>
std::string toString(const T& value, int width, char fill) {
    std::ostringstream out;
    out << std::setfill(fill) << std::setw(width) << value;
    return out.str();
}

// 精度、幅、フィル文字を指定して文字列に変換
// 例: toString(3.14, 2, 6, '0') → "003.14"
template <class T>
std::string toString(const T& value, int precision, int width, char fill) {
    std::ostringstream out;
    out << std::fixed << std::setfill(fill) << std::setw(width) << std::setprecision(precision) << value;
    return out.str();
}

// vectorを文字列に変換
// 例: toString(vector{1,2,3}) → "{1, 2, 3}"
template <class T>
std::string toString(const std::vector<T>& values) {
    std::ostringstream out;
    out << "{";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out << ", ";
        out << values[i];
    }
    out << "}";
    return out.str();
}

// ---------------------------------------------------------------------------
// 文字列から数値への変換
// ---------------------------------------------------------------------------

// 文字列を整数に変換
inline int toInt(const std::string& str) {
    try {
        return std::stoi(str);
    } catch (...) {
        return 0;
    }
}

// 文字列を64bit整数に変換
inline int64_t toInt64(const std::string& str) {
    try {
        return std::stoll(str);
    } catch (...) {
        return 0;
    }
}

// 文字列をfloatに変換
inline float toFloat(const std::string& str) {
    try {
        return std::stof(str);
    } catch (...) {
        return 0.0f;
    }
}

// 文字列をdoubleに変換
inline double toDouble(const std::string& str) {
    try {
        return std::stod(str);
    } catch (...) {
        return 0.0;
    }
}

// 文字列をboolに変換（"true", "TRUE", "1" → true）
inline bool toBool(const std::string& str) {
    if (str.empty()) return false;
    // 小文字に変換して比較
    std::string lower = str;
    for (auto& c : lower) c = std::tolower(c);
    return (lower == "true" || lower == "1" || lower == "yes");
}

// ---------------------------------------------------------------------------
// toHex - 16進数文字列に変換
// ---------------------------------------------------------------------------

// 数値を16進数文字列に変換
template <class T>
std::string toHex(const T& value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

// 整数を16進数文字列に変換（ゼロパディング）
inline std::string toHex(int value, int width = 0) {
    std::ostringstream out;
    if (width > 0) {
        out << std::setfill('0') << std::setw(width);
    }
    out << std::hex << std::uppercase << value;
    return out.str();
}

// 文字列を16進数文字列に変換
inline std::string toHex(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        out << std::setfill('0') << std::setw(2) << std::hex << std::uppercase << (int)c;
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// toBinary - 2進数文字列に変換
// ---------------------------------------------------------------------------

// 整数を2進数文字列に変換
inline std::string toBinary(int value) {
    return std::bitset<32>(value).to_string();
}

inline std::string toBinary(unsigned int value) {
    return std::bitset<32>(value).to_string();
}

inline std::string toBinary(char value) {
    return std::bitset<8>(value).to_string();
}

inline std::string toBinary(unsigned char value) {
    return std::bitset<8>(value).to_string();
}

// 文字列を2進数文字列に変換（各文字を8bitで表現）
inline std::string toBinary(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if (!result.empty()) result += " ";
        result += std::bitset<8>(c).to_string();
    }
    return result;
}

// ---------------------------------------------------------------------------
// fromHex - 16進数文字列から数値に変換
// ---------------------------------------------------------------------------

inline int hexToInt(const std::string& hexStr) {
    try {
        return std::stoi(hexStr, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

inline unsigned int hexToUInt(const std::string& hexStr) {
    try {
        return std::stoul(hexStr, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

} // namespace trussc
