package com.example.calinmihai

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

class BluetoothHelper(
    private val targetName: String
) {
    private val sppUuid: UUID =
        UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    private var socket: BluetoothSocket? = null
    private var input: InputStream? = null
    private var output: OutputStream? = null

    private var readerThread: Thread? = null
    private val running = AtomicBoolean(false)

    @Volatile
    var lastStatusLine: String = ""

    @Volatile
    var isConnected: Boolean = false
        private set

    @SuppressLint("MissingPermission")
    fun connectLegacy(): Boolean {
        try {
            val adapter = BluetoothAdapter.getDefaultAdapter() ?: return false
            if (!adapter.isEnabled) return false

            val device = adapter.bondedDevices.firstOrNull {
                it.name == targetName
            } ?: return false

            adapter.cancelDiscovery()

            val s = device.createRfcommSocketToServiceRecord(sppUuid)
            s.connect()

            socket = s
            input = s.inputStream
            output = s.outputStream
            isConnected = true

            startReader()
            return true
        } catch (_: Throwable) {
            disconnect()
            return false
        }
    }

    fun disconnect() {
        running.set(false)
        try { readerThread?.interrupt() } catch (_: Throwable) {}
        try { input?.close() } catch (_: Throwable) {}
        try { output?.close() } catch (_: Throwable) {}
        try { socket?.close() } catch (_: Throwable) {}

        readerThread = null
        input = null
        output = null
        socket = null

        isConnected = false
    }

    fun sendMessage(msg: String) {
        try {
            output?.write(msg.toByteArray(Charsets.UTF_8))
            output?.flush()
        } catch (_: Throwable) {}
    }

    private fun startReader() {
        running.set(true)

        readerThread =
            Thread {
                val buf = ByteArray(256)
                val sb = StringBuilder()

                while (running.get()) {
                    try {
                        val n = input?.read(buf) ?: break
                        if (n <= 0) break

                        val chunk = String(buf, 0, n, Charsets.UTF_8)

                        for (c in chunk) {
                            if (c == '\n') {
                                val line = sb.toString().trim()
                                sb.setLength(0)
                                if (line.isNotEmpty()) {
                                    lastStatusLine = line
                                }
                            } else {
                                sb.append(c)
                            }
                        }
                    } catch (_: Throwable) {
                        break
                    }
                }

                isConnected = false
                running.set(false)
            }.also {
                it.isDaemon = true
                it.start()
            }
    }
}