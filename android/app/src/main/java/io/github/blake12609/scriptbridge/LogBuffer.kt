package io.github.blake12609.scriptbridge

import androidx.compose.runtime.mutableStateListOf
import java.util.concurrent.ConcurrentLinkedQueue

enum class LogKind { Script, App, Error }

data class LogLine(val id: Long, val text: String, val kind: LogKind)

/**
 * Collects log text from any thread; [drain] moves it into [lines] on the main
 * thread a few times a second, so a chatty script can't flood the UI.
 */
class LogBuffer {
    private val pending = ConcurrentLinkedQueue<Pair<String, LogKind?>>()
    @Volatile private var clearRequested = false
    private var nextId = 0L
    val lines = mutableStateListOf<LogLine>()

    /** Text from the engine: script output, or its own messages. */
    fun add(text: String) {
        pending.add(text to null)
    }
    fun app(text: String) {
        pending.add(text to LogKind.App)
    }
    fun error(text: String) {
        pending.add(text to LogKind.Error)
    }
    fun requestClear() {
        pending.clear()
        clearRequested = true
    }

    fun drain() {
        if (clearRequested) {
            clearRequested = false
            lines.clear()
        }
        val added = ArrayList<LogLine>()
        while (true) {
            val (text, kind) = pending.poll() ?: break
            for (line in text.split('\n')) {
                if (line.isEmpty()) continue
                added += LogLine(nextId++, line, kind ?: classify(line))
            }
        }
        if (added.isEmpty()) return
        if (lines.size + added.size <= MAX) {
            lines.addAll(added)
        } else {
            val keep = (lines + added).takeLast(MAX)
            lines.clear()
            lines.addAll(keep)
        }
    }

    private fun classify(line: String) = when {
        line.startsWith("Script error") -> LogKind.Error
        line.startsWith("[demo]") || line.startsWith("Script loaded") || line.startsWith("Script stopped") ||
            line.startsWith("This device") -> LogKind.App
        else -> LogKind.Script
    }

    private companion object {
        const val MAX = 400
    }
}
