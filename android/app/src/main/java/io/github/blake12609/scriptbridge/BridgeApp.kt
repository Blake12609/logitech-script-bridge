package io.github.blake12609.scriptbridge

import android.app.Application

class BridgeApp : Application() {
    override fun onCreate() {
        super.onCreate()
        Bridge.init(this)
    }
}
