package com.espdrone.controller.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/*
 * Wire format of the ESPDrone ESP-NOW link and of the USB bridge. A port of
 * tools/groundstation/espdrone_link.py, which mirrors
 * components/core/crazyflie/hal/interface/espnow_link.h and
 * examples/espnow_bridge/main/bridge_protocol.h.
 *
 * Every frame: magic 0xD7, version 1, type, flags, ..., CRC-16/CCITT-FALSE of the bytes before
 * it. Little endian. Pure Kotlin, no Android classes, so it runs in plain JVM unit tests.
 */
object Link {
    const val MAGIC = 0xD7
    const val VERSION = 1

    const val PKT_CONTROL = 1
    const val PKT_STATUS = 2
    const val PKT_TELEMETRY = 3
    const val PKT_BRIDGE_CONFIG = 0x10
    const val PKT_BRIDGE_INFO = 0x11

    const val FLAG_ARM = 1 shl 0
    const val FLAG_KILL = 1 shl 1

    const val STATUS_ARMED = 1 shl 0
    const val STATUS_KILLED = 1 shl 1
    const val STATUS_BATTERY_LOW = 1 shl 2
    const val STATUS_FAILSAFE = 1 shl 3

    const val BRIDGE_CONFIG_SAVE = 1 shl 0
    const val BRIDGE_INFO_UNICAST = 1 shl 0
    const val BRIDGE_INFO_HOST_ACTIVE = 1 shl 1
    const val BRIDGE_INFO_DRONE_ACTIVE = 1 shl 2

    const val AXIS_MAX = 1000     // roll, pitch, yaw: -1000..1000
    const val THRUST_MAX = 1000   // thrust: 0..1000
    const val BAUDRATE = 460800

    /** Length of each frame type, CRC included. */
    val frameLength: Map<Int, Int> = mapOf(
        PKT_CONTROL to 16,
        PKT_STATUS to 10,
        PKT_TELEMETRY to 32,
        PKT_BRIDGE_CONFIG to 14,
        PKT_BRIDGE_INFO to 28,
    )
}

/** CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no reflection, no final xor. */
fun crc16(data: ByteArray, length: Int = data.size): Int {
    var crc = 0xFFFF
    for (i in 0 until length) {
        crc = crc xor ((data[i].toInt() and 0xFF) shl 8)
        repeat(8) {
            crc = if ((crc and 0x8000) != 0) (crc shl 1) xor 0x1021 else crc shl 1
            crc = crc and 0xFFFF
        }
    }
    return crc
}

sealed interface Frame

/** Transmitter to drone. Sticks as the drone expects them, see espnow-link.md. */
data class ControlFrame(
    val seq: Int,
    val roll: Int,       // -1000..1000, positive = right
    val pitch: Int,      // -1000..1000, positive = forward
    val yaw: Int,        // -1000..1000, positive = clockwise rate
    val thrust: Int,     // 0..1000
    val arm: Boolean,
    val kill: Boolean,
) : Frame {
    fun toBytes(): ByteArray {
        val flags = (if (arm) Link.FLAG_ARM else 0) or (if (kill) Link.FLAG_KILL else 0)
        val b = header(Link.PKT_CONTROL, flags)
        b.putShort((seq and 0xFFFF).toShort())
        b.putShort(roll.coerceIn(-Link.AXIS_MAX, Link.AXIS_MAX).toShort())
        b.putShort(pitch.coerceIn(-Link.AXIS_MAX, Link.AXIS_MAX).toShort())
        b.putShort(yaw.coerceIn(-Link.AXIS_MAX, Link.AXIS_MAX).toShort())
        b.putShort(thrust.coerceIn(0, Link.THRUST_MAX).toShort())
        return seal(b)
    }
}

/** Drone to transmitter, 10 Hz. */
data class StatusFrame(val flags: Int, val seqAck: Int, val batteryMv: Int) : Frame {
    val armed get() = (flags and Link.STATUS_ARMED) != 0
    val killed get() = (flags and Link.STATUS_KILLED) != 0
    val batteryLow get() = (flags and Link.STATUS_BATTERY_LOW) != 0
    val failsafe get() = (flags and Link.STATUS_FAILSAFE) != 0
    val batteryVolts get() = batteryMv / 1000f
}

/** Drone to transmitter, 20 Hz. Attitude in degrees, rates in deg/s. */
data class TelemetryFrame(
    val flags: Int,
    val seq: Int,
    val rollDeg: Float,
    val pitchDeg: Float,
    val yawDeg: Float,
    val gyroDps: List<Float>,
    val thrust: Int,
    val motors: List<Int>,
    val batteryMv: Int,
) : Frame {
    val batteryVolts get() = batteryMv / 1000f
}

/** Bridge to host, 2 Hz. */
data class BridgeInfo(
    val flags: Int,
    val staMac: ByteArray,     // the bridge's STA MAC: the drone's ESPNOW_CTRL_PEER_MAC
    val droneMac: ByteArray,
    val channel: Int,
    val txOk: Int,
    val txFail: Int,
    val hostTimeouts: Int,
    val rxFromDrone: Int,
) : Frame {
    val unicast get() = (flags and Link.BRIDGE_INFO_UNICAST) != 0
    val hostActive get() = (flags and Link.BRIDGE_INFO_HOST_ACTIVE) != 0
    val droneActive get() = (flags and Link.BRIDGE_INFO_DRONE_ACTIVE) != 0
    val staMacText get() = formatMac(staMac)
    val droneMacText get() = if (unicast) formatMac(droneMac) else "broadcast"

    override fun equals(other: Any?): Boolean =
        other is BridgeInfo && flags == other.flags && staMac.contentEquals(other.staMac) &&
            droneMac.contentEquals(other.droneMac) && channel == other.channel && txOk == other.txOk &&
            txFail == other.txFail && hostTimeouts == other.hostTimeouts && rxFromDrone == other.rxFromDrone

    override fun hashCode(): Int = flags * 31 + staMac.contentHashCode() + droneMac.contentHashCode() + txOk
}

/** Host to bridge: which drone and channel. droneMac all zero means broadcast. */
data class BridgeConfig(
    val flags: Int,
    val droneMac: ByteArray,
    val channel: Int,
) : Frame {
    fun toBytes(): ByteArray {
        require(droneMac.size == 6 && channel in 1..13) { "need a 6-byte MAC and a channel from 1 to 13" }
        val b = header(Link.PKT_BRIDGE_CONFIG, flags)
        b.put(droneMac)
        b.put(channel.toByte())
        b.put(0)
        return seal(b)
    }

    override fun equals(other: Any?): Boolean =
        other is BridgeConfig && flags == other.flags && droneMac.contentEquals(other.droneMac) &&
            channel == other.channel

    override fun hashCode(): Int = flags * 31 + droneMac.contentHashCode() + channel
}

fun formatMac(mac: ByteArray): String = mac.joinToString(":") { "%02x".format(it.toInt() and 0xFF) }

fun parseMac(text: String): ByteArray {
    val parts = text.trim().split(':')
    require(parts.size == 6) { "MAC must look like aa:bb:cc:dd:ee:ff" }
    return ByteArray(6) { parts[it].toInt(16).toByte() }
}

/** Little endian frame buffer of the right size, with magic, version, type and flags written. */
private fun header(type: Int, flags: Int): ByteBuffer {
    val b = ByteBuffer.allocate(Link.frameLength.getValue(type)).order(ByteOrder.LITTLE_ENDIAN)
    b.put(Link.MAGIC.toByte())
    b.put(Link.VERSION.toByte())
    b.put(type.toByte())
    b.put(flags.toByte())
    return b
}

/** Appends the CRC and returns the frame. The buffer must be filled up to its last two bytes. */
private fun seal(b: ByteBuffer): ByteArray {
    val bytes = b.array()
    check(b.position() == bytes.size - 2) { "frame body has the wrong size" }
    b.putShort(crc16(bytes, bytes.size - 2).toShort())
    return bytes
}

private fun ByteBuffer.u16(): Int = short.toInt() and 0xFFFF
private fun ByteBuffer.u8(): Int = get().toInt() and 0xFF

/** Returns the frame for a complete, valid frame, null otherwise (wrong magic, length or CRC). */
fun decodeFrame(raw: ByteArray): Frame? {
    if (raw.size < 5 || (raw[0].toInt() and 0xFF) != Link.MAGIC || (raw[1].toInt() and 0xFF) != Link.VERSION) {
        return null
    }
    val type = raw[2].toInt() and 0xFF
    if (Link.frameLength[type] != raw.size) {
        return null
    }
    val crc = (raw[raw.size - 2].toInt() and 0xFF) or ((raw[raw.size - 1].toInt() and 0xFF) shl 8)
    if (crc != crc16(raw, raw.size - 2)) {
        return null
    }

    val b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
    b.position(3)
    val flags = b.u8()
    return when (type) {
        Link.PKT_CONTROL -> {
            val seq = b.u16()
            ControlFrame(
                seq = seq, roll = b.short.toInt(), pitch = b.short.toInt(), yaw = b.short.toInt(),
                thrust = b.u16(), arm = (flags and Link.FLAG_ARM) != 0, kill = (flags and Link.FLAG_KILL) != 0,
            )
        }
        Link.PKT_STATUS -> StatusFrame(flags = flags, seqAck = b.u16(), batteryMv = b.u16())
        Link.PKT_TELEMETRY -> {
            val seq = b.u16()
            val roll = b.short / 100f
            val pitch = b.short / 100f
            val yaw = b.short / 100f
            val gyro = listOf(b.short / 10f, b.short / 10f, b.short / 10f)
            val thrust = b.u16()
            val motors = listOf(b.u16(), b.u16(), b.u16(), b.u16())
            TelemetryFrame(flags, seq, roll, pitch, yaw, gyro, thrust, motors, b.u16())
        }
        Link.PKT_BRIDGE_CONFIG -> {
            val mac = ByteArray(6).also { b.get(it) }
            BridgeConfig(flags, mac, b.u8())
        }
        Link.PKT_BRIDGE_INFO -> {
            val sta = ByteArray(6).also { b.get(it) }
            val drone = ByteArray(6).also { b.get(it) }
            val channel = b.u8()
            b.get()   // reserved
            BridgeInfo(flags, sta, drone, channel, b.u16(), b.u16(), b.u16(), b.u16())
        }
        else -> null
    }
}

/** Splits a byte stream into frames and resynchronises on noise, such as the board's boot log. */
class FrameParser {
    private val buf = ByteArray(256)
    private var fill = 0

    /** Bytes thrown away while looking for a frame. */
    var droppedBytes = 0L
        private set

    fun feed(data: ByteArray, length: Int = data.size): List<Frame> {
        val out = ArrayList<Frame>()
        var offset = 0
        while (offset < length) {
            val n = minOf(buf.size - fill, length - offset)
            System.arraycopy(data, offset, buf, fill, n)
            fill += n
            offset += n
            parse(out)   // leaves less than one frame in the buffer, so there is room again
        }
        return out
    }

    private fun parse(out: MutableList<Frame>) {
        while (fill > 0) {
            if ((buf[0].toInt() and 0xFF) != Link.MAGIC) {
                drop(1)
                continue
            }
            if (fill < 3) break
            val length = Link.frameLength[buf[2].toInt() and 0xFF]
            if (length == null) {
                drop(1)
                continue
            }
            if (fill < length) break
            val frame = decodeFrame(buf.copyOf(length))
            if (frame == null) {
                drop(1)
                continue
            }
            out += frame
            remove(length)
        }
    }

    private fun drop(n: Int) {
        remove(n)
        droppedBytes += n
    }

    private fun remove(n: Int) {
        System.arraycopy(buf, n, buf, 0, fill - n)
        fill -= n
    }
}
