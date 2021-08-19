#include <AppWin32.hh>
#include <impl/Library.hh>
#include <impl/JNILocal.hh>
#include <iostream>

// Globally accessible instance
// Will be created as soon as java runtime load this library
jwm::AppWin32 jwm::AppWin32::gInstance;

void jwm::AppWin32::init(JNIEnv *jniEnv) {
    _jniEnv = jniEnv;

    if (!_windowManager.init()) {
        jclass Exception = _jniEnv->FindClass("java/lang/Exception");
        _jniEnv->ThrowNew(Exception, "Failed to initialize Win32 Window Manager");
    }
}

int jwm::AppWin32::start() {
    JNIEnv* env = getJniEnv();

    int result = 0;

    while (!isTerminateRequested()) {
        result = _windowManager.iteration();

        if (result)
            break;

        std::vector<jobject> toProcess;
        std::swap(toProcess, _uiThreadCallbacks);

        for (auto callback: toProcess) {
            classes::Runnable::run(env, callback);
            env->DeleteGlobalRef(callback);
        }
    }

    // Release enqueued but not executed callbacks
    for (auto callback: _uiThreadCallbacks)
        env->DeleteGlobalRef(callback);

    return result;
}

void jwm::AppWin32::terminate() {
    _terminateRequested.store(true);
}

bool jwm::AppWin32::isTerminateRequested() const {
    return _terminateRequested.load();
}

void jwm::AppWin32::enqueueCallback(jobject callback) {
    _uiThreadCallbacks.push_back(callback);
}

const std::vector<jwm::ScreenWin32> &jwm::AppWin32::getScreens() {
    _screens.clear();
    EnumDisplayMonitors(nullptr, nullptr, (MONITORENUMPROC) enumMonitorFunc, 0);
    return _screens;
}

BOOL jwm::AppWin32::enumMonitorFunc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    ScreenWin32 screen = ScreenWin32::fromHMonitor(monitor);
    AppWin32::getInstance()._screens.push_back(screen);
    return TRUE;
}

// JNI

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_jwm_App__1nInit
        (JNIEnv* env, jclass jclass) {
    jwm::AppWin32::getInstance().init(env);
}

extern "C" JNIEXPORT jint JNICALL Java_org_jetbrains_jwm_App__1nStart
        (JNIEnv* env, jclass jclass) {
    return jwm::AppWin32::getInstance().start();
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_jwm_App__1nTerminate
        (JNIEnv* env, jclass jclass) {
    jwm::AppWin32::getInstance().terminate();
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_jetbrains_jwm_App__1nGetScreens
        (JNIEnv* env, jclass jclass) {
    auto& app = jwm::AppWin32::getInstance();
    auto& screens = app.getScreens();
    auto screensCount = static_cast<jsize>(screens.size());

    jobjectArray array = env->NewObjectArray(screensCount, jwm::classes::Screen::kCls, nullptr);

    if (jwm::classes::Throwable::exceptionThrown(env))
        return nullptr;

    for (jsize i = 0; i < screensCount; i++) {
        const jwm::ScreenWin32& screenData = screens[i];
        jwm::JNILocal<jobject> screen(env, screenData.toJni(env));
        env->SetObjectArrayElement(array, i, screen.get());
    }

    return array;
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_jwm_App__1nRunOnUIThread
        (JNIEnv* env, jclass cls, jobject callback) {
    jwm::AppWin32& app = jwm::AppWin32::getInstance();
    jobject callbackRef = env->NewGlobalRef(callback);
    app.enqueueCallback(callbackRef);
}
