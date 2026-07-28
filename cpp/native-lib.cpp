#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>

// Макросы для вывода из C++ в android Logcat
#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ВНИМАНИЕ: android_1poc (с единичкой!), потому что в пакете есть подчеркивание _
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {

    LOGI("=== C++ NATIVE CODE ЗАПУЩЕН ===");

    size_t malicious_size = (size_t)-1;  // SIZE_MAX
    char *buffer = (char *)malloc(malicious_size);

    if (buffer == NULL) {
        LOGI("C++: malloc(SIZE_MAX) вернул NULL. Память не выделилась.");

        malicious_size = 0xFFFFFFFF;
        buffer = (char *)malloc(malicious_size);

        if (buffer == NULL) {
            LOGI("C++: malloc(0xFFFFFFFF) ТОЖЕ вернул NULL. Защита сработала.");
        }
    }

    if (buffer != NULL) {
        memset(buffer, 0x41, malicious_size);
        free(buffer);
        LOGI("C++: Удалось записать в память!");
    }

    std::string hello = "Hello from C++ (Смотри Logcat!)";
    return env->NewStringUTF(hello.c_str());
}