package com.espdrone.controller.control

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FlightControlTest {

    private val now = 10_000L

    @Test
    fun startsDisarmedAndSendsNothing() {
        val c = FlightControl()
        val f = c.nextControl(5)
        assertEquals(5, f.seq)
        assertFalse(f.arm)
        assertFalse(f.kill)
        assertEquals(0, f.thrust)
    }

    @Test
    fun armingNeedsThrottleAtZero() {
        val c = FlightControl()
        c.setThrottle(0.5f)
        assertEquals(ArmResult.THRUST_NOT_ZERO, c.arm(now))
        assertFalse(c.armed)
        c.setThrottle(0f)
        assertEquals(ArmResult.ARMED, c.arm(now))
        assertEquals(ArmResult.ALREADY_ARMED, c.arm(now))
    }

    @Test
    fun thrustIsOnlySentWhileArmed() {
        val c = FlightControl(Settings(maxThrustPercent = 100, stickPercent = 100))
        c.setThrottle(0.4f)
        assertEquals(0, c.nextControl(0).thrust)
        c.setThrottle(0f)
        c.arm(now)
        c.setThrottle(0.4f)
        assertEquals(400, c.nextControl(1).thrust)
        assertTrue(c.nextControl(1).arm)
    }

    @Test
    fun maxThrustPercentScalesTheOutput() {
        val c = FlightControl(Settings(maxThrustPercent = 50, stickPercent = 100))
        c.arm(now)
        c.setThrottle(1f)
        assertEquals(500, c.nextControl(0).thrust)
    }

    @Test
    fun sticksAreScaledAndClamped() {
        val c = FlightControl(Settings(maxThrustPercent = 50, stickPercent = 60))
        c.setSticks(1f, -1f, 0.5f)
        val f = c.nextControl(0)
        assertEquals(600, f.roll)
        assertEquals(-600, f.pitch)
        assertEquals(300, f.yaw)
        c.setSticks(9f, -9f, 0f)
        assertEquals(600, c.nextControl(0).roll)
    }

    @Test
    fun killLatchesAndStopsThrust() {
        val c = FlightControl(Settings(100, 100))
        c.arm(now)
        c.setThrottle(0.7f)
        c.kill()
        val f = c.nextControl(0)
        assertTrue(f.kill)
        assertFalse(f.arm)
        assertEquals(0, f.thrust)
        assertEquals(ArmResult.KILLED, c.arm(now))
        c.setThrottle(0.5f)
        assertEquals(0f, c.throttle, 0f)

        c.releaseKill()
        assertFalse(c.nextControl(1).kill)
        assertEquals(ArmResult.ARMED, c.arm(now))
    }

    @Test
    fun forcedZeroTellsTheScreenToResetItsStick() {
        val c = FlightControl()
        val before = c.resetCount
        c.setThrottle(0.3f)
        c.disarm()
        assertEquals(0f, c.throttle, 0f)
        assertTrue(c.resetCount > before)
    }

    @Test
    fun leavingTheScreenDisarmsAndCentresEverything() {
        val c = FlightControl(Settings(100, 100))
        c.arm(now)
        c.setThrottle(0.6f)
        c.setSticks(1f, 1f, 1f)
        c.onScreenLost()
        val f = c.nextControl(0)
        assertFalse(f.arm)
        assertEquals(0, f.thrust)
        assertEquals(0, f.roll)
        assertEquals(0, f.pitch)
        assertEquals(0, f.yaw)
    }

    @Test
    fun dropsOurArmedStateWhenTheDroneDoesNotFollow() {
        val c = FlightControl()
        c.arm(now)
        // within the grace period the drone may not have processed the ARM edge yet
        c.onDroneStatus(droneArmed = false, droneKilled = false, droneFailsafe = false, nowMs = now + 300)
        assertTrue(c.armed)
        c.onDroneStatus(droneArmed = false, droneKilled = false, droneFailsafe = false, nowMs = now + 700)
        assertFalse(c.armed)
        assertFalse(c.nextControl(0).arm)
    }

    @Test
    fun keepsArmedWhenTheDroneConfirms() {
        val c = FlightControl()
        c.arm(now)
        c.onDroneStatus(droneArmed = true, droneKilled = false, droneFailsafe = false, nowMs = now + 5000)
        assertTrue(c.armed)
    }

    @Test
    fun failsafeAndForeignKillDisarm() {
        val c = FlightControl()
        c.arm(now)
        c.onDroneStatus(droneArmed = true, droneKilled = false, droneFailsafe = true, nowMs = now + 10)
        assertFalse(c.armed)

        c.arm(now)
        c.onDroneStatus(droneArmed = true, droneKilled = true, droneFailsafe = false, nowMs = now + 10)
        assertTrue(c.killed)
        assertFalse(c.armed)
    }
}
