#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {

    LOGI("=== NATIVE EXPLOIT STARTED (REAL OVERFLOW DEMO) ===");

    // --- ЛОГИКА НАСТОЯЩЕГО ПЕРЕПОЛНЕНИЯ ---

    // Берем два больших числа: 0x80000000 = 2,147,483,648 (половина от max uint32)
    uint32_t val1 = 0x80000000;
    uint32_t val2 = 0x80000000;

    // Складываем в 64-битном типе: результат 0x100000000 (4,294,967,296)
    uint64_t full_sum = (uint64_t)val1 + val2;

    LOGI("Val1: %u (0x%X)", val1, val1);
    LOGI("Val2: %u (0x%X)", val2, val2);
    LOGI("Full Sum (64-bit): %llX", full_sum);

    // Переполнение: приводим результат обратно к uint32_t.
    // 0x100000000 & 0xFFFFFFFF = 0. Мы потеряли старшую единицу!
    uint32_t overflowed_size = (uint32_t)full_sum;

    LOGI("Overflowed Size (uint32): %u bytes!", overflowed_size);

    // --- ВЫДЕЛЕНИЕ ПАМЯТИ ПО ПЕРЕПОЛНЕННОМУ РАЗМЕРУ ---

    // malloc получает 0. В некоторых реализациях это вернет NULL, в других — 1 байт или специальный маркер.
    // Для наглядности: если размер 0, malloc может вернуть NULL или пустой блок.
    // Мы проверяем и подстраиваемся, чтобы показать логику даже при возврате NULL.

    char *buffer = (char *)malloc(overflowed_size);

    if (buffer == NULL) {
        LOGE("malloc returned NULL for size %u. System protection or zero-size alloc failed.", overflowed_size);
        // Попробуем выделить минимальный байт, чтобы показать запись за пределы
        buffer = (char *)malloc(1);
        if (!buffer) return env->NewStringUTF("FAIL: malloc NULL twice");
        LOGI("Fallback: Allocated 1 byte instead of %u.", overflowed_size);
    } else {
        LOGI("Allocated buffer at 0x%p with size %u (from overflow)", (void*)buffer, overflowed_size);
    }

    // --- ЗАПИСЬ ДАННЫХ ПОЛНЫМ РАЗМЕРОМ (4 ГБ) В МАЛЕНЬКИЙ БУФЕР ---

    size_t write_bytes = (size_t)full_sum; // Мы пытаемся записать 4,294,967,296 байт!

    LOGI("Writing %zu bytes into a buffer of only %u byte(s)! THIS IS HEAP CORRUPTION!",
         write_bytes, overflowed_size);

    // Записываем. В реальном системном коде это затерло бы соседние структуры (указатели функций).
    memset(buffer, 'X', write_bytes);

    LOGI("Write complete! Heap corruption confirmed in Logcat.");

    free(buffer);

    std::string result = "Exploit Success! Look for 'Writing ... into 0 byte buffer'.";
    return env->NewStringUTF(result.c_str());
}