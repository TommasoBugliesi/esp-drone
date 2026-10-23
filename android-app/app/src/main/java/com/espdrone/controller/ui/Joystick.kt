package com.espdrone.controller.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput

/**
 * A square joystick. [onChange] gets x and y in -1..1, y positive = up.
 *
 * A spring axis returns to zero when the finger is lifted. An axis without spring stays where it
 * was left: the throttle stick uses springY = false and starts at the bottom (startY = -1).
 * Changing [resetSignal] puts the stick back to its start (used when the app forces throttle to zero).
 * Each stick tracks its own finger, so both can be used at the same time.
 */
@Composable
fun Joystick(
    modifier: Modifier = Modifier,
    springX: Boolean = true,
    springY: Boolean = true,
    startY: Float = 0f,
    resetSignal: Int = 0,
    onChange: (x: Float, y: Float) -> Unit,
) {
    var x by remember { mutableFloatStateOf(0f) }
    var y by remember { mutableFloatStateOf(startY) }
    val currentOnChange by rememberUpdatedState(onChange)

    LaunchedEffect(resetSignal) {
        x = 0f
        y = startY
        currentOnChange(x, y)
    }

    Canvas(
        modifier = modifier.pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                val cx = size.width / 2f
                val cy = size.height / 2f
                val rx = size.width * TRAVEL
                val ry = size.height * TRAVEL

                fun move(p: Offset) {
                    x = ((p.x - cx) / rx).coerceIn(-1f, 1f)
                    y = (-(p.y - cy) / ry).coerceIn(-1f, 1f)
                    currentOnChange(x, y)
                }

                move(down.position)
                do {
                    val event = awaitPointerEvent()
                    val change = event.changes.firstOrNull { it.id == down.id } ?: break
                    if (change.pressed) {
                        move(change.position)
                        change.consume()
                    }
                } while (change.pressed)

                if (springX) x = 0f
                if (springY) y = 0f
                currentOnChange(x, y)
            }
        },
    ) {
        val rx = size.width * TRAVEL
        val ry = size.height * TRAVEL
        val centre = Offset(size.width / 2f, size.height / 2f)

        drawRoundRect(
            color = Color(0xFF1E252D),
            size = size,
            cornerRadius = CornerRadius(24f, 24f),
        )
        drawRoundRect(
            color = Color(0xFF3A4654),
            topLeft = Offset(centre.x - rx, centre.y - ry),
            size = Size(rx * 2, ry * 2),
            cornerRadius = CornerRadius(16f, 16f),
            style = Stroke(width = 3f),
        )
        drawLine(Color(0xFF3A4654), Offset(centre.x - rx, centre.y), Offset(centre.x + rx, centre.y), 2f)
        drawLine(Color(0xFF3A4654), Offset(centre.x, centre.y - ry), Offset(centre.x, centre.y + ry), 2f)
        drawCircle(
            color = Color(0xFF6FB3FF),
            radius = minOf(size.width, size.height) * 0.09f,
            center = Offset(centre.x + x * rx, centre.y - y * ry),
        )
    }
}

/** Half the travel of a stick, as a share of the view size. */
private const val TRAVEL = 0.40f
