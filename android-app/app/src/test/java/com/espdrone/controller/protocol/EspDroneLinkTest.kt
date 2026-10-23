package com.espdrone.controller.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The hex strings are produced by tools/groundstation/espdrone_link.py, which is checked against
 * the C structs of espnow_link.h. If this test and test_espdrone_link.py both pass, Kotlin, Python
 * and the firmware agree on the wire format.
 */
class EspDroneLinkTest {

    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(2 * it, 2 * it + 2).toInt(16).toByte() }

    @Test
    fun crcCheckValue() {
        assertEquals(0x29B1, crc16("123456789".toByteArray()))
    }

    @Test
    fun frameLengthsMatchTheCStructs() {
        assertEquals(16, Link.frameLength[Link.PKT_CONTROL])
        assertEquals(10, Link.frameLength[Link.PKT_STATUS])
        assertEquals(32, Link.frameLength[Link.PKT_TELEMETRY])
        assertEquals(14, Link.frameLength[Link.PKT_BRIDGE_CONFIG])
        assertEquals(28, Link.frameLength[Link.PKT_BRIDGE_INFO])
    }

    @Test
    fun controlFramesMatchTheGoldenBytes() {
        assertArrayEquals(
            hex("D7010102070001000200030004008E02"),
            ControlFrame(7, 1, 2, 3, 4, arm = false, kill = true).toBytes(),
        )
        assertArrayEquals(
            hex("D70101013412E80318FCFFFFF4010C36"),
            ControlFrame(0x1234, 1000, -1000, -1, 500, arm = true, kill = false).toBytes(),
        )
        assertArrayEquals(
            hex("D7010100FFFF000000000000000075C4"),
            ControlFrame(0xFFFF, 0, 0, 0, 0, arm = false, kill = false).toBytes(),
        )
    }

    @Test
    fun controlFrameClampsOutOfRangeSticks() {
        val bytes = ControlFrame(1, 5000, -5000, 0, 9999, arm = false, kill = false).toBytes()
        val back = decodeFrame(bytes) as ControlFrame
        assertEquals(1000, back.roll)
        assertEquals(-1000, back.pitch)
        assertEquals(1000, back.thrust)
    }

    @Test
    fun controlFrameRoundTrip() {
        val f = ControlFrame(0x1234, 1000, -1000, -1, 500, arm = true, kill = false)
        assertEquals(f, decodeFrame(f.toBytes()))
    }

    @Test
    fun statusFrame() {
        val s = decodeFrame(hex("D701020592105C2BFA66")) as StatusFrame
        assertTrue(s.armed)
        assertTrue(s.batteryLow)
        assertFalse(s.killed)
        assertFalse(s.failsafe)
        assertEquals(4242, s.seqAck)
        assertEquals(11.1f, s.batteryVolts, 1e-4f)
    }

    @Test
    fun telemetryFrame() {
        val t = decodeFrame(
            hex("D70103010900D204DAFD4F46E803FDFF00002C013000E803FF073C005C2BF4BD"),
        ) as TelemetryFrame
        assertEquals(9, t.seq)
        assertEquals(12.34f, t.rollDeg, 1e-4f)
        assertEquals(-5.5f, t.pitchDeg, 1e-4f)
        assertEquals(179.99f, t.yawDeg, 1e-3f)
        assertEquals(listOf(100.0f, -0.3f, 0.0f), t.gyroDps)
        assertEquals(300, t.thrust)
        assertEquals(listOf(48, 1000, 2047, 60), t.motors)
        assertEquals(11.1f, t.batteryVolts, 1e-4f)
    }

    @Test
    fun bridgeConfigMatchesTheGoldenBytes() {
        val config = BridgeConfig(Link.BRIDGE_CONFIG_SAVE, hex("D8132A145A2D"), 6)
        assertArrayEquals(hex("D7011001D8132A145A2D0600CEE1"), config.toBytes())
        assertEquals(config, decodeFrame(config.toBytes()))
    }

    @Test(expected = IllegalArgumentException::class)
    fun bridgeConfigRejectsChannel14() {
        BridgeConfig(0, ByteArray(6), 14).toBytes()
    }

    @Test
    fun bridgeInfo() {
        val i = decodeFrame(hex("D7011107E05A1BA618CCD8132A145A2D0600AA00020001006900B65F")) as BridgeInfo
        assertTrue(i.unicast)
        assertTrue(i.hostActive)
        assertTrue(i.droneActive)
        assertEquals("e0:5a:1b:a6:18:cc", i.staMacText)
        assertEquals("d8:13:2a:14:5a:2d", i.droneMacText)
        assertEquals(6, i.channel)
        assertEquals(170, i.txOk)
        assertEquals(2, i.txFail)
        assertEquals(1, i.hostTimeouts)
        assertEquals(105, i.rxFromDrone)
    }

    @Test
    fun corruptedFrameIsRejected() {
        val bytes = hex("D701020592105C2BFA66")
        bytes[5] = (bytes[5].toInt() xor 1).toByte()
        assertNull(decodeFrame(bytes))
    }

    @Test
    fun macHelpers() {
        assertEquals("24:6f:28:aa:bb:cc", formatMac(parseMac("24:6F:28:aa:bb:cc")))
    }

    @Test
    fun parserResynchronisesAfterNoise() {
        val parser = FrameParser()
        val status = hex("D701020592105C2BFA66")
        val noise = "ets Jun  8 2016 00:22:57\r\n".toByteArray() + hex("D70102") + "garbage".toByteArray()

        assertTrue(parser.feed(noise + status.copyOfRange(0, 4)).isEmpty())
        val frames = parser.feed(status.copyOfRange(4, status.size) + hex("D70101013412E80318FCFFFFF4010C36"))

        assertEquals(2, frames.size)
        assertTrue(frames[0] is StatusFrame)
        assertTrue(frames[1] is ControlFrame)
        assertTrue(parser.droppedBytes > 0)
    }

    @Test
    fun parserHandlesAFrameDeliveredOneByteAtATime() {
        val parser = FrameParser()
        val frames = ArrayList<Frame>()
        for (b in hex("D70103010900D204DAFD4F46E803FDFF00002C013000E803FF073C005C2BF4BD")) {
            frames += parser.feed(byteArrayOf(b))
        }
        assertEquals(1, frames.size)
        assertTrue(frames[0] is TelemetryFrame)
        assertEquals(0L, parser.droppedBytes)
    }

    @Test
    fun parserHandlesAHugeChunk() {
        val parser = FrameParser()
        val one = hex("D701020592105C2BFA66")
        val big = ByteArray(one.size * 100) { one[it % one.size] }
        assertEquals(100, parser.feed(big).size)
    }
}
