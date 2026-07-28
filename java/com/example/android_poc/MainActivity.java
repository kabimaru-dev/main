package com.example.android_poc;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final String TAG = "CVE-2025-48595";

    // ВАЖНО: Имя должно совпадать с project("android_poc") в CMakeLists.txt!
    static {
        System.loadLibrary("android_poc");
    }

    public native String stringFromJNI();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 1. Вызываем C++ код
        String cppResult = stringFromJNI();
        Log.i(TAG, "Результат из C++: " + cppResult);
        Toast.makeText(this, "C++ ответил: " + cppResult, Toast.LENGTH_LONG).show();

        // 2. Вызываем Java код с переполнением
        triggerIntegerOverflow();
    }

    private void triggerIntegerOverflow() {
        try {
            Log.i(TAG, "Начинаем Java эксплойт...");

            int maliciousValue = Integer.MAX_VALUE;
            int[] buffer = new int[1024];

            for (int i = 0; i < maliciousValue; i++) {
                buffer[i] = i;
            }

        } catch (Exception e) {
            Log.e(TAG, "ПОЙМАЛИ ИСКЛЮЧЕНИЕ: " + e.toString());
            e.printStackTrace(); // Печатает весь стектрейс красным цветом
            Toast.makeText(this, "Java упала: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }
}