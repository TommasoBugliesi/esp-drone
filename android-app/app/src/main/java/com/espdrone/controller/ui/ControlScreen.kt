package com.espdrone.controller.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.espdrone.controller.ControllerViewModel
import com.espdrone.controller.control.FlightControl

private val Good = Color(0xFF7DDC7D)
private val Warn = Color(0xFFFFC83C)
private val Bad = Color(0xFFFF5A5A)
private val Dim = Color(0xFF9AA5B1)

/**
 * Landscape layout: left stick (throttle up/down, yaw left/right), the middle column with the
 * state, telemetry and the arm and kill buttons, right stick (pitch up/down, roll left/right).
 */
@Composable
fun ControlScreen(vm: ControllerViewModel) {
    val ui by vm.ui.collectAsState()

    Row(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF12161B))
            .padding(12.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Left: throttle (hold) and yaw (spring)
        Joystick(
            modifier = Modifier
                .weight(1f)
                .aspectRatio(1f),
            springX = true,
            springY = false,
            startY = -1f,
            resetSignal = ui.resetCount,
            onChange = { x, y ->
                vm.setThrottle((y + 1f) / 2f)
                vm.setYawInput(x)
            },
        )

        Column(
            modifier = Modifier
                .weight(1.5f)
                .fillMaxHeight(),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            val s = ui.status
            val linkText = when {
                !ui.usbConnected -> ui.usbText
                ui.bridge == null -> "Bridge not answering yet..."
                !ui.droneLinkUp -> "NO LINK TO THE DRONE"
                else -> "Drone linked"
            }
            val linkColor = when {
                !ui.usbConnected || ui.bridge == null -> Dim
                !ui.droneLinkUp -> Bad
                else -> Good
            }
            Text(linkText, color = linkColor, fontSize = 20.sp)

            val stateText = when {
                ui.killed -> "KILLED"
                ui.armed -> "ARMED"
                else -> "disarmed"
            }
            val stateColor = when {
                ui.killed -> Bad
                ui.armed -> Warn
                else -> Good
            }
            Text(
                stateText + (s?.let { "   %.2f V".format(it.batteryVolts) } ?: ""),
                color = stateColor,
                fontSize = 26.sp,
            )
            if (s?.batteryLow == true) Text("BATTERY LOW", color = Bad)
            if (s?.failsafe == true) Text("DRONE FAILSAFE", color = Bad)

            val t = ui.telemetry
            Mono(
                if (t != null) {
                    "att  %7.2f %7.2f %7.2f deg".format(t.rollDeg, t.pitchDeg, t.yawDeg)
                } else {
                    "att  -"
                },
            )
            Mono(
                if (t != null) {
                    "gyro %7.1f %7.1f %7.1f deg/s".format(t.gyroDps[0], t.gyroDps[1], t.gyroDps[2])
                } else {
                    "gyro -"
                },
            )
            Mono(
                if (t != null) {
                    "mixer thrust %4d  motors %s".format(t.thrust, t.motors.joinToString(" ") { "%4d".format(it) })
                } else {
                    "mixer -"
                },
            )
            ui.bridge?.let {
                Mono("bridge ${it.staMacText} -> ${it.droneMacText} ch ${it.channel}  sent ${it.txOk} from drone ${it.rxFromDrone}", Dim)
            }

            Text(ui.message, color = Color(0xFFB4C8FF), fontSize = 14.sp)
            Spacer(Modifier.weight(1f))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                Button(
                    onClick = vm::toggleArm,
                    enabled = !ui.killed && (ui.armed || ui.throttle <= FlightControl.ZERO),
                    modifier = Modifier
                        .weight(1f)
                        .height(56.dp),
                ) {
                    Text(if (ui.armed) "DISARM" else "ARM")
                }
                if (ui.killed) {
                    OutlinedButton(onClick = vm::releaseKill, modifier = Modifier.weight(1f).height(56.dp)) {
                        Text("RELEASE KILL")
                    }
                } else {
                    Button(
                        onClick = vm::kill,
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFB3261E)),
                        modifier = Modifier
                            .weight(1f)
                            .height(56.dp),
                    ) {
                        Text("KILL")
                    }
                }
                OutlinedButton(
                    onClick = vm::connect,
                    enabled = !ui.usbConnected,
                    modifier = Modifier.height(56.dp),
                ) {
                    Text("USB")
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("max thrust ${ui.settings.maxThrustPercent}%", color = Dim, fontSize = 12.sp,
                    modifier = Modifier.size(width = 110.dp, height = 20.dp))
                Slider(
                    value = ui.settings.maxThrustPercent.toFloat(),
                    onValueChange = { vm.setSettings(ui.settings.copy(maxThrustPercent = it.toInt())) },
                    valueRange = 10f..100f,
                    modifier = Modifier.weight(1f),
                )
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("stick ${ui.settings.stickPercent}%", color = Dim, fontSize = 12.sp,
                    modifier = Modifier.size(width = 110.dp, height = 20.dp))
                Slider(
                    value = ui.settings.stickPercent.toFloat(),
                    onValueChange = { vm.setSettings(ui.settings.copy(stickPercent = it.toInt())) },
                    valueRange = 10f..100f,
                    modifier = Modifier.weight(1f),
                )
            }
        }

        // Right: pitch (up/down) and roll (left/right), both spring
        Joystick(
            modifier = Modifier
                .weight(1f)
                .aspectRatio(1f),
            springX = true,
            springY = true,
            resetSignal = 0,
            onChange = { x, y -> vm.setRollPitch(roll = x, pitch = y) },
        )
    }
}

@Composable
private fun Mono(text: String, color: Color = Color(0xFFD8DEE6)) {
    Text(text, color = color, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
}
