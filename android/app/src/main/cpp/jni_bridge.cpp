// JNI glue between NativeBridge.kt and MobileSession. Text crosses the boundary
// as UTF-8 byte arrays (JNI's own string encoding can't carry every character).
#include <jni.h>
#include <pthread.h>

#include <memory>
#include <mutex>
#include <string>

#include "mobile_session.h"

namespace {

JavaVM* gVm = nullptr;
jclass gBridge = nullptr;  // global ref to NativeBridge
jmethodID gOnWrite = nullptr, gOnLog = nullptr, gOnClearLog = nullptr;
pthread_key_t gDetachKey;

// The engine calls back from its own threads; attach them to the VM on first use
// and detach them again when they end.
JNIEnv* env() {
    JNIEnv* e = nullptr;
    if (gVm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK) return e;
#ifdef __ANDROID__
    if (gVm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
#else  // desktop JDKs declare it with void** (used by the host test)
    if (gVm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr) != JNI_OK) return nullptr;
#endif
    pthread_setspecific(gDetachKey, gVm);
    return e;
}

jbyteArray toBytes(JNIEnv* e, const std::string& s) {
    jbyteArray a = e->NewByteArray(static_cast<jsize>(s.size()));
    if (a) e->SetByteArrayRegion(a, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return a;
}

std::string fromBytes(JNIEnv* e, jbyteArray a) {
    if (!a) return {};
    const jsize n = e->GetArrayLength(a);
    std::string s(static_cast<size_t>(n), '\0');
    if (n) e->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(&s[0]));
    return s;
}

void callWithBytes(jmethodID m, const std::string& s) {
    JNIEnv* e = env();
    if (!e) return;
    jbyteArray a = toBytes(e, s);
    if (!a) return;
    e->CallStaticVoidMethod(gBridge, m, a);
    if (e->ExceptionCheck()) e->ExceptionClear();
    e->DeleteLocalRef(a);
}

MobileSession& session() {
    static MobileSession* s = [] {
        auto* m = new MobileSession([](const std::string& bytes) { callWithBytes(gOnWrite, bytes); },
                                    [](const std::string& text) { callWithBytes(gOnLog, text); });
        m->onClearLog = [] {
            if (JNIEnv* e = env()) {
                e->CallStaticVoidMethod(gBridge, gOnClearLog);
                if (e->ExceptionCheck()) e->ExceptionClear();
            }
        };
        return m;
    }();
    return *s;
}

MobileDevice device(jint d) {
    return d >= 0 && d <= static_cast<jint>(MobileDevice::Demo) ? static_cast<MobileDevice>(d) : MobileDevice::Demo;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    gVm = vm;
    JNIEnv* e = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    pthread_key_create(&gDetachKey, [](void* v) { static_cast<JavaVM*>(v)->DetachCurrentThread(); });
    jclass c = e->FindClass("io/github/blake12609/scriptbridge/NativeBridge");
    if (!c) return JNI_ERR;
    gBridge = static_cast<jclass>(e->NewGlobalRef(c));
    gOnWrite = e->GetStaticMethodID(c, "onWrite", "([B)V");
    gOnLog = e->GetStaticMethodID(c, "onLog", "([B)V");
    gOnClearLog = e->GetStaticMethodID(c, "onClearLog", "()V");
    if (!gOnWrite || !gOnLog || !gOnClearLog) return JNI_ERR;
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_start(
    JNIEnv* e, jclass, jbyteArray script, jbyteArray name, jint dev, jdouble jitterMin, jdouble jitterMax,
    jint screenW, jint screenH) {
    MobileOptions o;
    o.device = device(dev);
    o.jitterMin = jitterMin;
    o.jitterMax = jitterMax;
    o.screenWidth = screenW;
    o.screenHeight = screenH;
    std::string error;
    if (session().start(fromBytes(e, script), fromBytes(e, name), o, error)) return nullptr;
    // errors are plain ASCII apart from the script name; keep it safe for NewStringUTF
    for (char& ch : error)
        if (static_cast<unsigned char>(ch) >= 0x80) ch = '?';
    return e->NewStringUTF(error.c_str());
}

JNIEXPORT void JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_stop(JNIEnv*, jclass) {
    session().stop();
}

JNIEXPORT jboolean JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_isRunning(JNIEnv*, jclass) {
    return session().running() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_setDevice(JNIEnv*, jclass, jint dev) {
    session().setDevice(device(dev));
}

JNIEXPORT jbyteArray JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_connectCommands(JNIEnv* e, jclass,
                                                                                                 jint dev) {
    return toBytes(e, MobileSession::connectCommands(device(dev)));
}

JNIEXPORT void JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_onSerialData(JNIEnv* e, jclass,
                                                                                        jbyteArray data) {
    const std::string bytes = fromBytes(e, data);
    session().onSerialData(bytes.data(), bytes.size());
}

JNIEXPORT void JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_touchButton(JNIEnv*, jclass, jint n,
                                                                                       jboolean pressed) {
    session().onTouchButton(n, pressed);
}

JNIEXPORT void JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_gKey(JNIEnv*, jclass, jint n,
                                                                                jboolean pressed) {
    session().onGKey(n, pressed);
}

JNIEXPORT void JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_setKeys(JNIEnv*, jclass, jint mask) {
    session().setKeys(mask);
}

JNIEXPORT jint JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_buttonMask(JNIEnv*, jclass) {
    return session().buttonMask();
}

JNIEXPORT jbyteArray JNICALL Java_io_github_blake12609_scriptbridge_NativeBridge_checkSyntax(JNIEnv* e, jclass,
                                                                                             jbyteArray script,
                                                                                             jbyteArray name) {
    return toBytes(e, Engine::checkSyntax(fromBytes(e, script), fromBytes(e, name)));
}

}  // extern "C"
