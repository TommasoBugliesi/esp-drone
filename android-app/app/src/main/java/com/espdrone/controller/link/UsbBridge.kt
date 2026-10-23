package com.espdrone.controller.link

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import androidx.core.content.ContextCompat
import com.espdrone.controller.protocol.Frame
import com.espdrone.controller.protocol.FrameParser
import com.espdrone.controller.protocol.Link
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager

/**
 * The ESP-NOW bridge on a USB OTG cable: finds a USB serial adapter, asks for permission, opens
 * it at the bridge's baud rate, and turns the bytes into frames.
 *
 * Opening the port can reset the board (DTR/RTS drive its auto-reset circuit on most CP210x and
 * CH34x boards), so the caller must not send before the bridge's first BridgeInfo frame arrives.
 * Callbacks come from background threads.
 */
class UsbBridge(
    private val context: Context,
    private val listener: Listener,
) : SerialInputOutputManager.Listener {

    interface Listener {
        fun onFrame(frame: Frame)
        fun onConnection(connected: Boolean, text: String)
    }

    private val usb = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val parser = FrameParser()
    private var port: UsbSerialPort? = null
    private var io: SerialInputOutputManager? = null
    private var receiverRegistered = false

    val isOpen: Boolean get() = io != null

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                connect()
            } else {
                listener.onConnection(false, "USB permission denied")
            }
        }
    }

    /** Opens the first USB serial adapter found; asks for permission first if needed. */
    fun connect() {
        if (isOpen) return
        val driver: UsbSerialDriver? = UsbSerialProber.getDefaultProber().findAllDrivers(usb).firstOrNull()
        if (driver == null) {
            listener.onConnection(false, "No USB serial adapter found (OTG cable plugged in?)")
            return
        }
        if (!usb.hasPermission(driver.device)) {
            requestPermission(driver.device)
            return
        }
        open(driver)
    }

    private fun requestPermission(device: UsbDevice) {
        if (!receiverRegistered) {
            ContextCompat.registerReceiver(
                context, permissionReceiver, IntentFilter(ACTION_USB_PERMISSION),
                ContextCompat.RECEIVER_NOT_EXPORTED,
            )
            receiverRegistered = true
        }
        // Mutable: the system fills in the granted flag. Explicit package: required on Android 14.
        val intent = Intent(ACTION_USB_PERMISSION).setPackage(context.packageName)
        val pending = PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_MUTABLE)
        listener.onConnection(false, "Waiting for USB permission...")
        usb.requestPermission(device, pending)
    }

    private fun open(driver: UsbSerialDriver) {
        val connection = usb.openDevice(driver.device)
        if (connection == null) {
            listener.onConnection(false, "Cannot open the USB device")
            return
        }
        try {
            val p = driver.ports[0]
            p.open(connection)
            p.setParameters(Link.BAUDRATE, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            try {
                p.dtr = false
                p.rts = false
            } catch (_: UnsupportedOperationException) {
                // not every driver can drive the lines
            }
            port = p
            io = SerialInputOutputManager(p, this).also { it.start() }
            listener.onConnection(true, "USB connected, waiting for the bridge...")
        } catch (e: Exception) {
            close()
            listener.onConnection(false, "USB error: ${e.message}")
        }
    }

    /** Queues bytes for the bridge. Safe to call from any thread. */
    fun send(bytes: ByteArray) {
        io?.writeAsync(bytes)
    }

    fun disconnect() {
        close()
        listener.onConnection(false, "Disconnected")
    }

    fun release() {
        close()
        if (receiverRegistered) {
            context.unregisterReceiver(permissionReceiver)
            receiverRegistered = false
        }
    }

    private fun close() {
        io?.stop()
        io = null
        try {
            port?.close()
        } catch (_: Exception) {
        }
        port = null
    }

    override fun onNewData(data: ByteArray) {
        for (frame in parser.feed(data)) {
            listener.onFrame(frame)
        }
    }

    override fun onRunError(e: Exception) {
        close()
        listener.onConnection(false, "USB lost: ${e.message}")
    }

    companion object {
        private const val ACTION_USB_PERMISSION = "com.espdrone.controller.USB_PERMISSION"
    }
}
