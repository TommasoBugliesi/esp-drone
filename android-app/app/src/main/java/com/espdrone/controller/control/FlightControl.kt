package com.espdrone.controller.control

import com.espdrone.controller.protocol.ControlFrame
import com.espdrone.controller.protocol.Link
import kotlin.math.roundToInt

/** User-adjustable limits. The drone applies its own limits on top (ESPNOW_CTRL_THRUST_MAX etc.). */
data class Settings(
    /** Share of the drone's thrust range that full throttle on the screen asks for. */
    val maxThrustPercent: Int = 50,
    /** Share of full deflection that a fully pushed stick sends for roll, pitch and yaw. */
    val stickPercent: Int = 60,
)

enum class ArmResult { ARMED, ALREADY_ARMED, KILLED, THRUST_NOT_ZERO }

/**
 * The state behind the screen: sticks, throttle, arm and kill, and the rules that keep the
 * drone safe. No Android classes, no clock of its own (time is passed in), so it is unit
 * tested on the JVM.
 *
 * Rules, matching the drone's ESP-NOW link (docs/handbook/espnow-link.md):
 *  - arming needs throttle at zero (the drone refuses ARM with thrust > 0)
 *  - thrust is only sent while armed and not killed
 *  - kill latches until it is released
 *  - losing the screen (background, lost focus, USB unplugged) disarms and zeroes everything
 *  - if the drone says it is not armed shortly after we asked it to arm, we drop our armed state, so
 *    a failsafe on the drone is never followed by a surprise re-arm
 */
class FlightControl(settings: Settings = Settings()) {

    @Volatile
    var settings: Settings = settings

    var armed = false
        private set
    var killed = false
        private set

    /** 0..1 of the allowed range, set by the throttle stick. */
    var throttle = 0f
        private set

    /** Incremented whenever the throttle was forced to zero, so the screen can reset its stick. */
    var resetCount = 0
        private set

    private var roll = 0f
    private var pitch = 0f
    private var yaw = 0f
    private var armedAtMs = 0L

    @Synchronized
    fun setSticks(roll: Float, pitch: Float, yaw: Float) {
        this.roll = roll.coerceIn(-1f, 1f)
        this.pitch = pitch.coerceIn(-1f, 1f)
        this.yaw = yaw.coerceIn(-1f, 1f)
    }

    @Synchronized
    fun setThrottle(value: Float) {
        throttle = if (killed) 0f else value.coerceIn(0f, 1f)
    }

    @Synchronized
    fun arm(nowMs: Long): ArmResult = when {
        killed -> ArmResult.KILLED
        armed -> ArmResult.ALREADY_ARMED
        throttle > ZERO -> ArmResult.THRUST_NOT_ZERO
        else -> {
            armed = true
            armedAtMs = nowMs
            ArmResult.ARMED
        }
    }

    @Synchronized
    fun disarm() {
        armed = false
        forceThrottleZero()
    }

    /** Stops the motors at once and stays latched until [releaseKill]. */
    @Synchronized
    fun kill() {
        killed = true
        armed = false
        forceThrottleZero()
    }

    @Synchronized
    fun releaseKill() {
        killed = false
        armed = false
        forceThrottleZero()
    }

    /** The screen is gone, or the link to the bridge: sticks centred, throttle zero, disarmed. */
    @Synchronized
    fun onScreenLost() {
        roll = 0f
        pitch = 0f
        yaw = 0f
        armed = false
        forceThrottleZero()
    }

    @Synchronized
    fun onDroneStatus(droneArmed: Boolean, droneKilled: Boolean, droneFailsafe: Boolean, nowMs: Long) {
        if (droneKilled && !killed) {
            // killed from elsewhere (for example another transmitter)
            killed = true
            armed = false
            forceThrottleZero()
        }
        if (droneFailsafe) {
            armed = false
            forceThrottleZero()
        }
        if (armed && !droneArmed && nowMs - armedAtMs > ARM_CONFIRM_MS) {
            armed = false
            forceThrottleZero()
        }
    }

    /** The control frame to send now. */
    @Synchronized
    fun nextControl(seq: Int): ControlFrame {
        val s = settings
        val stick = s.stickPercent.coerceIn(0, 100) / 100f
        val flying = armed && !killed
        val thrust = if (flying) {
            (throttle * s.maxThrustPercent.coerceIn(0, 100) / 100f * Link.THRUST_MAX).roundToInt()
        } else {
            0
        }
        return ControlFrame(
            seq = seq and 0xFFFF,
            roll = (roll * stick * Link.AXIS_MAX).roundToInt(),
            pitch = (pitch * stick * Link.AXIS_MAX).roundToInt(),
            yaw = (yaw * stick * Link.AXIS_MAX).roundToInt(),
            thrust = thrust,
            arm = armed,
            kill = killed,
        )
    }

    private fun forceThrottleZero() {
        throttle = 0f
        resetCount++
    }

    companion object {
        const val ZERO = 0.02f
        const val ARM_CONFIRM_MS = 600L
    }
}
