// =============================================================================
// tcxCurl_android.cpp - Android backend for tcx::HttpClient
// =============================================================================
// The Android NDK doesn't ship libcurl. Rather than vendoring third-party C
// libraries, we delegate to the platform's own HTTP stack via JNI ->
// java.net.HttpURLConnection. A thin Java helper (AndroidHttpClient.java)
// does the InputStream draining and byte[] marshalling — see that class for
// the ABI we depend on.
//
// Threading: HttpURLConnection throws NetworkOnMainThreadException if it
// runs on the Android UI thread. tcx::HttpClient::request() is expected to
// be called from a worker thread by its only consumers today (the
// tcxOpenStreetMap tile worker and Nominatim search worker). We don't try to
// enforce that here — same as the libcurl path, which would also block the
// main thread if you misused it. We DO need to attach the current thread to
// the JVM, since worker threads are not attached by default.
// =============================================================================

#include "tcxCurl.h"

#ifdef TCX_HTTP_ANDROID

#include <jni.h>
#include <android/native_activity.h>
#include "sokol_app.h"

#include <cstring>

namespace {

struct JniScope {
    JNIEnv* env = nullptr;
    JavaVM* vm  = nullptr;
    bool    attached = false;

    JniScope() {
        auto* activity = (ANativeActivity*)sapp_android_get_native_activity();
        if (!activity) return;
        vm = activity->vm;
        jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            } else {
                env = nullptr;
            }
        } else if (res != JNI_OK) {
            env = nullptr;
        }
    }
    ~JniScope() { if (attached && vm) vm->DetachCurrentThread(); }
    explicit operator bool() const { return env != nullptr; }
};

// Resolve com/trussc/tcx/curl/AndroidHttpClient via the app's ClassLoader.
// FindClass on a non-Java thread only sees system classes, so we route
// through the activity's ClassLoader (the same trick AndroidUsbDataSource
// relies on implicitly when it's called from the activity thread).
jclass findHelperClass(JNIEnv* env) {
    // First try the direct route; it works when the call is happening on the
    // main Android thread (e.g. from the activity callbacks). On worker
    // threads it fails for app classes — fall through to ClassLoader.
    jclass cls = env->FindClass("com/trussc/tcx/curl/AndroidHttpClient");
    if (cls != nullptr) return cls;
    env->ExceptionClear();

    auto* activity = (ANativeActivity*)sapp_android_get_native_activity();
    if (!activity) return nullptr;

    jclass activityCls = env->GetObjectClass(activity->clazz);
    jmethodID getClassLoader = env->GetMethodID(
        activityCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallObjectMethod(activity->clazz, getClassLoader);
    env->DeleteLocalRef(activityCls);
    if (!loader || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    jclass loaderCls = env->GetObjectClass(loader);
    jmethodID loadClass = env->GetMethodID(
        loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("com.trussc.tcx.curl.AndroidHttpClient");
    cls = (jclass)env->CallObjectMethod(loader, loadClass, name);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loaderCls);
    env->DeleteLocalRef(loader);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return cls;
}

// Convert std::vector<pair<string,string>> -> two parallel Java String[]s.
// Returned arrays are local refs; caller deletes.
void buildHeaderArrays(JNIEnv* env,
                       const std::vector<std::pair<std::string,std::string>>& headers,
                       jobjectArray& outNames,
                       jobjectArray& outValues) {
    jclass strCls = env->FindClass("java/lang/String");
    outNames  = env->NewObjectArray((jsize)headers.size(), strCls, nullptr);
    outValues = env->NewObjectArray((jsize)headers.size(), strCls, nullptr);
    env->DeleteLocalRef(strCls);

    for (jsize i = 0; i < (jsize)headers.size(); i++) {
        jstring k = env->NewStringUTF(headers[i].first.c_str());
        jstring v = env->NewStringUTF(headers[i].second.c_str());
        env->SetObjectArrayElement(outNames,  i, k);
        env->SetObjectArrayElement(outValues, i, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
    }
}

tcx::HttpResponse callJavaRequest(const std::string& method,
                                  const std::string& url,
                                  const std::vector<std::pair<std::string,std::string>>& headers,
                                  const std::string& body,
                                  long timeoutSeconds) {
    tcx::HttpResponse out;

    JniScope jni;
    if (!jni) {
        out.error = "tcxCurl[android]: no JNIEnv (sokol native activity not initialised?)";
        return out;
    }
    JNIEnv* env = jni.env;

    jclass helper = findHelperClass(env);
    if (!helper) {
        out.error = "tcxCurl[android]: AndroidHttpClient class not found "
                    "(addon Java sources not packaged?)";
        return out;
    }

    jmethodID requestMid = env->GetStaticMethodID(
        helper, "request",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[BII)"
        "Lcom/trussc/tcx/curl/AndroidHttpClient$Result;");
    if (!requestMid) {
        env->ExceptionClear();
        env->DeleteLocalRef(helper);
        out.error = "tcxCurl[android]: AndroidHttpClient.request method missing";
        return out;
    }

    jstring jMethod = env->NewStringUTF(method.c_str());
    jstring jUrl    = env->NewStringUTF(url.c_str());

    jobjectArray jHeaderNames  = nullptr;
    jobjectArray jHeaderValues = nullptr;
    buildHeaderArrays(env, headers, jHeaderNames, jHeaderValues);

    jbyteArray jBody = nullptr;
    if (!body.empty()) {
        jBody = env->NewByteArray((jsize)body.size());
        env->SetByteArrayRegion(jBody, 0, (jsize)body.size(),
                                reinterpret_cast<const jbyte*>(body.data()));
    }

    // Connect timeout is fixed-ish (10s in the libcurl path); use it. The
    // total timeoutSeconds becomes the per-read timeout, which matches
    // libcurl's CURLOPT_TIMEOUT semantics well enough for our consumers.
    jint connectMs = 10000;
    jint readMs    = (jint)(timeoutSeconds > 0 ? timeoutSeconds * 1000 : 30000);

    jobject result = env->CallStaticObjectMethod(
        helper, requestMid,
        jMethod, jUrl, jHeaderNames, jHeaderValues, jBody, connectMs, readMs);

    env->DeleteLocalRef(jMethod);
    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(jHeaderNames);
    env->DeleteLocalRef(jHeaderValues);
    if (jBody) env->DeleteLocalRef(jBody);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(helper);
        out.error = "tcxCurl[android]: AndroidHttpClient.request threw";
        return out;
    }
    if (!result) {
        env->DeleteLocalRef(helper);
        out.error = "tcxCurl[android]: AndroidHttpClient.request returned null";
        return out;
    }

    jclass resultCls = env->GetObjectClass(result);
    jfieldID fStatus = env->GetFieldID(resultCls, "statusCode", "I");
    jfieldID fBody   = env->GetFieldID(resultCls, "body",       "[B");
    jfieldID fError  = env->GetFieldID(resultCls, "error",      "Ljava/lang/String;");

    out.statusCode = env->GetIntField(result, fStatus);

    auto bodyArr = (jbyteArray)env->GetObjectField(result, fBody);
    if (bodyArr) {
        jsize len = env->GetArrayLength(bodyArr);
        if (len > 0) {
            out.body.resize((size_t)len);
            env->GetByteArrayRegion(bodyArr, 0, len,
                                    reinterpret_cast<jbyte*>(&out.body[0]));
        }
        env->DeleteLocalRef(bodyArr);
    }

    auto err = (jstring)env->GetObjectField(result, fError);
    if (err) {
        const char* c = env->GetStringUTFChars(err, nullptr);
        if (c) out.error = c;
        env->ReleaseStringUTFChars(err, c);
        env->DeleteLocalRef(err);
    }

    env->DeleteLocalRef(resultCls);
    env->DeleteLocalRef(result);
    env->DeleteLocalRef(helper);
    return out;
}

} // namespace

namespace tcx::curl {

HttpResponse HttpClient::request(const std::string& method,
                                 const std::string& path,
                                 const std::string& body,
                                 const std::string& contentType) {
    std::string url = baseUrl_ + path;

    // Compose effective headers: the user's own list plus Content-Type for
    // requests carrying a body. We pass by value to avoid mutating headers_.
    std::vector<std::pair<std::string,std::string>> hs = headers_;
    if (!body.empty()) {
        bool hasCT = false;
        for (auto& h : hs) {
            if (h.first == "Content-Type") { hasCT = true; break; }
        }
        if (!hasCT) hs.push_back({"Content-Type", contentType});
    }
    return callJavaRequest(method, url, hs, body, timeoutSeconds_);
}

HttpResponse HttpClient::uploadFile(const std::string& path, const std::string& filePath) {
    // Multipart upload via HttpURLConnection is mechanical but verbose
    // (boundary string, CRLF framing, base64-free byte payload). Nothing in
    // RideFormDeck or tcxOpenStreetMap uses uploadFile, so we stub for now;
    // wire this in if/when a consumer needs it.
    (void)path; (void)filePath;
    HttpResponse out;
    out.error = "tcxCurl[android]: uploadFile() not implemented";
    return out;
}

} // namespace tcx::curl

#endif // TCX_HTTP_ANDROID
