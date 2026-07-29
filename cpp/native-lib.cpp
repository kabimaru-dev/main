#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "CVE-2025-48595"
#define VULN_CONSTANT 64 // Например, размер заголовка структуры

extern "C"
JNIEXPORT void JNICALL
Java_com_example_android_1poc_MainActivity_triggerIntegerOverflowNative(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "[*] CVE-2025-48595 - Real Overflow Trigger");

    // 1. Входные данные: пользователь передаёт значение, которое при сложении переполнится
    size_t user_input = SIZE_MAX - VULN_CONSTANT + 1;
    size_t calculated_size = user_input + VULN_CONSTANT; // 0 или малое число из-за переполнения

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "[*] User input: %zu", user_input);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "[!] Calculated size (overflowed): %zu", calculated_size);

    // 2. Выделение памяти на основе ПЕРЕПОЛНЕННОГО размера
    char *buffer = (char *)malloc(calculated_size);

    if (buffer == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "[!] malloc returned NULL: %zu", calculated_size);
        return;
    }

    // 3. КРИТИЧЕСКАЯ ОШИБКА: Используем ИСХОДНЫЙ размер для записи!
    size_t write_size = user_input + VULN_CONSTANT; // Программа думает, что выделяла это

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "[*] Writing %zu bytes into buffer of actual size %zu",
                        write_size, calculated_size);

    if (write_size > calculated_size) {
        memset(buffer, 0x41, write_size); // ЗАПИСЬ ВНЕ ПРЕДЕЛОВ!

        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "[+] SCUDO SHOULD CATCH THIS!");
        // Здесь SCUDO проверит канарку и вызовет abort() с сообщением о corrupted heap
    }

    free(buffer);
}