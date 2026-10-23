package com.espdrone.controller

import android.app.Application
import android.os.SystemClock
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.espdrone.controller.control.ArmResult
import com.espdrone.controller.control.FlightControl
import com.espdrone.controller.control.Settings
import com.espdrone.controller.link.UsbBridge
import com.espdrone.controller.protocol.BridgeInfo
import com.espdrone.controller.protocol.Frame
import com.espdrone.controller.protocol.StatusFrame
import com.espdrone.controller.protocol.TelemetryFrame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class UiState(
    val usbConnected: Boolean = false,
    val usbText: String = "Not connected",
    val bridge: BridgeInfo? = null,
    val droneLinkUp: Boolean = false,
    val status: StatusFrame? = null,
    val telemetry: TelemetryFrame? = null,
    val armed: Boolean = false,
    val killed: Boolean = false,
    val throttle: Float = 0f,
    val resetCount: Int = 0,
    val settings: Settings = Settings(),
    val message: String = "Connect the bridge with an OTG cable. Props off for the first test!",
)

/**
 * Runs the 50 Hz send loop and owns the USB link. The loop only sends while the screen is in the
 * foreground, the USB link is open and the bridge has answered. When the screen goes,
 * a few disarm frames are sent and then nothing: the drone's failsafe takes over.
 */
class ControllerViewModel(app: Application) : AndroidViewModel(app), UsbBridge.Listener {

    val control = FlightControl()

    private val _ui = MutableStateFlow(UiState())
    val ui: StateFlow<UiState> = _ui

    private val bridge = UsbBridge(app, this)
    private val lock = Any()

    private var seq = 0
    @Volatile private var message = _ui.value.message

    @Volatile private var foreground = false
    @Volatile private var bridgeReady = false

    // Latest frames from the bridge, written from the USB thread
    private var bridgeInfo: BridgeInfo? = null
    private var status: StatusFrame? = null
    private var statusAtMs = 0L
    private var telemetry: TelemetryFrame? = null
    private var telemetryAtMs = 0L
    private var usbConnected = false
    private var usbText = _ui.value.usbText

    init {
        viewModelScope.launch(Dispatchers.Default) {
            while (isActive) {
                if (canSend()) {
                    bridge.send(control.nextControl(seq++).toBytes())
                }
                publish()
                delay(SEND_PERIOD_MS)
            }
        }
    }

    private fun canSend() = foreground && bridge.isOpen && bridgeReady

    // --- lifecycle, called by the activity

    fun connect() = bridge.connect()

    fun onForeground() {
        foreground = true
        connect()
    }

    /** Disarm, then a short burst of safe frames, then silence. */
    fun onBackground() {
        clearInputs()
        control.onScreenLost()
        foreground = false
        if (bridge.isOpen && bridgeReady) {
            viewModelScope.launch(Dispatchers.Default) {
                repeat(5) {
                    bridge.send(control.nextControl(seq++).toBytes())
                    delay(SEND_PERIOD_MS)
                }
            }
        }
        say("Screen left: disarmed, no frames are sent. The drone's failsafe takes over.")
    }

    /** Notification shade, a dialog or a split screen: disarm and zero, but keep sending safe frames. */
    fun onFocusChanged(hasFocus: Boolean) {
        if (!hasFocus) {
            clearInputs()
            control.onScreenLost()
            say("Focus lost: disarmed, throttle zero.")
        }
    }

    override fun onCleared() {
        bridge.release()
    }

    // --- screen events

    // The right stick gives roll and pitch, the left stick gives yaw: remember both halves
    private var rollIn = 0f
    private var pitchIn = 0f
    private var yawIn = 0f

    fun setRollPitch(roll: Float, pitch: Float) {
        rollIn = roll
        pitchIn = pitch
        control.setSticks(rollIn, pitchIn, yawIn)
    }

    fun setYawInput(yaw: Float) {
        yawIn = yaw
        control.setSticks(rollIn, pitchIn, yawIn)
    }

    private fun clearInputs() {
        rollIn = 0f
        pitchIn = 0f
        yawIn = 0f
    }

    fun setThrottle(value: Float) = control.setThrottle(value)

    fun toggleArm() {
        if (control.armed) {
            control.disarm()
            say("Disarmed")
            return
        }
        say(
            when (control.arm(SystemClock.elapsedRealtime())) {
                ArmResult.ARMED -> "ARM sent"
                ArmResult.ALREADY_ARMED -> "Already armed"
                ArmResult.KILLED -> "Release the kill first"
                ArmResult.THRUST_NOT_ZERO -> "Arming refused: bring the throttle to zero first"
            },
        )
    }

    fun kill() {
        control.kill()
        say("KILL sent (latched). Release it to arm again.")
    }

    fun releaseKill() {
        control.releaseKill()
        say("Kill released, disarmed")
    }

    fun setSettings(settings: Settings) {
        control.settings = settings
    }

    // --- frames from the bridge (USB thread)

    override fun onFrame(frame: Frame) {
        val now = SystemClock.elapsedRealtime()
        synchronized(lock) {
            when (frame) {
                is BridgeInfo -> {
                    bridgeInfo = frame
                    bridgeReady = true
                }
                is StatusFrame -> {
                    status = frame
                    statusAtMs = now
                    control.onDroneStatus(frame.armed, frame.killed, frame.failsafe, now)
                }
                is TelemetryFrame -> {
                    telemetry = frame
                    telemetryAtMs = now
                }
                else -> Unit
            }
        }
    }

    override fun onConnection(connected: Boolean, text: String) {
        synchronized(lock) {
            usbConnected = connected
            usbText = text
            if (!connected) {
                bridgeReady = false
                bridgeInfo = null
                status = null
                telemetry = null
            }
        }
        if (!connected) {
            clearInputs()
            control.onScreenLost()
        }
    }

    private fun say(text: String) {
        message = text
    }

    private fun publish() {
        val now = SystemClock.elapsedRealtime()
        synchronized(lock) {
            _ui.value = UiState(
                usbConnected = usbConnected,
                usbText = usbText,
                bridge = bridgeInfo,
                droneLinkUp = status != null && now - statusAtMs < LINK_TIMEOUT_MS,
                status = status,
                telemetry = if (now - telemetryAtMs < LINK_TIMEOUT_MS) telemetry else null,
                armed = control.armed,
                killed = control.killed,
                throttle = control.throttle,
                resetCount = control.resetCount,
                settings = control.settings,
                message = message,
            )
        }
    }

    companion object {
        const val SEND_PERIOD_MS = 20L       // 50 Hz
        const val LINK_TIMEOUT_MS = 500L
    }
}
