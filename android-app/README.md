# ESPDrone Controller (Android)

Fly the drone from a phone: two on-screen sticks, arm and kill buttons, live telemetry. The phone talks to the
[ESP-NOW USB bridge](../examples/espnow_bridge) over a **USB OTG cable**; the bridge talks to the drone.
Video stays on a second phone, so this screen is only the controls.

```
phone ──USB OTG──► ESP32 bridge ──ESP-NOW──► drone
```

**State: first draft, never built.** This machine had no Kotlin compiler, Gradle or Android SDK, so
nothing here has been compiled or run. What *is* checked: the protocol code and the safety rules have JVM unit
tests whose expected bytes come from the Python implementation, which is itself checked against the firmware
structs (see [Verification](#verification)). Expect to fix a few compile errors on the first build.

## Build and run

1. Install Android Studio (a recent version) and open this folder (`android-app/`).
2. Let it sync. It will offer to update the Android Gradle plugin and the library versions in
   `build.gradle.kts`: accept, the versions in the files are only a starting point. It also creates the Gradle
   wrapper (`gradle wrapper` does the same from a shell).
3. Run the unit tests first: `./gradlew testDebugUnitTest`
4. Build and install on the phone (USB debugging on): `./gradlew installDebug`

Requirements on the phone: Android 8.0 or newer (`minSdk 26`), USB host support (most phones; needs an OTG
cable or adapter). The bridge needs power: a phone supplies enough for an ESP32 board in most cases; if it
resets, use a powered OTG hub.

## Using it

1. Flash the bridge and pair it with the drone once, from the laptop (see
   [Ground station](../docs/handbook/ground-station.md)). The pairing is saved in the bridge.
2. Plug the bridge into the phone. Android asks to open the app and for USB permission: accept. (Or open the app
   and press **USB**.)
3. The top line goes from "waiting for the bridge" to "Drone linked" when the drone answers. Opening the port
   resets many boards, so the first seconds are normal.
4. Props off, throttle stick down, press **ARM**. Raise the throttle slowly.

| Control | Action |
|---|---|
| Left stick, up/down | throttle. **Stays where you leave it**, starts at the bottom |
| Left stick, left/right | yaw, springs back |
| Right stick | pitch (up/down) and roll (left/right), springs back |
| ARM / DISARM | arming needs the throttle at zero, the button is disabled otherwise |
| KILL | stops the motors at once and latches. **RELEASE KILL** then needs ARM again |
| max thrust, stick | sliders: share of the drone's thrust range and of full deflection that the sticks send |

## Safety behaviour

The drone has its own protections (arm on a rising edge with thrust 0, kill latch, 250 ms link failsafe, thrust
ceiling `ESPNOW_CTRL_THRUST_MAX`, see [ESP-NOW link](../docs/handbook/espnow-link.md)). The app adds:

| Situation | What the app does |
|---|---|
| App goes to the background, screen locks | zero throttle, disarm, five safe frames, then **stops sending**: the drone's failsafe acts |
| Window loses focus (notification shade, dialog) | zero throttle and disarm, keeps sending safe frames |
| USB unplugged or lost | zero throttle and disarm; nothing can be sent |
| Throttle forced to zero (disarm, kill, the cases above) | the on-screen throttle stick jumps back to the bottom |
| The drone reports "not armed" 0.6 s after the app armed (for example after a failsafe) | the app drops its armed state: no surprise re-arm |
| The drone reports failsafe, or killed by someone else | disarm and zero throttle |
| Before the bridge has answered | no control frames are sent, so a board that is rebooting is never fed stale input |
| Defaults | max thrust 50 %, stick 60 % |

The send loop is a coroutine at 50 Hz. It is not tied to touch events, so a still finger keeps the link alive
and a frozen UI does not.

## Layout

| Path | What |
|---|---|
| `protocol/EspDroneLink.kt` | wire format: CRC-16, control, status, telemetry, bridge frames, resynchronising `FrameParser`. Port of `tools/groundstation/espdrone_link.py`, no Android classes |
| `control/FlightControl.kt` | sticks, throttle, arm and kill state and every rule in the table above. Pure Kotlin, time is passed in |
| `link/UsbBridge.kt` | USB serial through [usb-serial-for-android](https://github.com/mik3y/usb-serial-for-android): finding the adapter, permission, 460800 baud, DTR/RTS low, frames in and out |
| `ControllerViewModel.kt` | the 50 Hz send loop, the lifecycle rules, the UI state |
| `ui/Joystick.kt` | square stick with optional spring per axis, one finger each |
| `ui/ControlScreen.kt` | landscape screen |
| `MainActivity.kt` | lifecycle: foreground, background, focus, USB attached |
| `res/xml/device_filter.xml` | USB ids that start the app: CP210x, CH340, CH9102, FTDI, ESP32-S3 USB |
| `src/test` | unit tests of the protocol and the control rules |

## Verification

| | |
|---|---|
| Wire format | `EspDroneLinkTest` has the exact bytes of control, status, telemetry, bridge config and bridge info frames, generated by `espdrone_link.py`. That module is tested against the C struct sizes in `tools/groundstation/test_espdrone_link.py`, and was used against the real bridge and the simulator. |
| Control rules | `FlightControlTest` |
| Compiles, unit tests pass | **not yet run** |
| USB open, permission, reading and writing on a real phone | **not yet run** |
| Joystick feel, layout on a real screen | **not yet run** |
| End to end with the drone | **not yet run**, do the bench checks in the ground-station page first, props off |

To try the app without the drone, run `tools/groundstation/sim_bridge.py` on a laptop. It makes a pseudo
terminal, so it needs a real USB serial adapter looped to the phone to be reachable: practical only with a
second ESP32 or a USB-UART adapter on the laptop wired TX to RX. The simplest first test is the real bridge with
the drone powered and props off.

## Next

| Step | |
|---|---|
| First build and fix compile errors | |
| Remember the sliders and the last settings | `DataStore` |
| Haptic feedback on arm and kill, a louder low-battery alert | |
| BLE instead of the cable | a GATT service in the bridge carrying the same frames; `UsbBridge` becomes one of two `Link` implementations |
| Second phone for video | independent of this app; the control phone only shows telemetry |
| Recording a flight (telemetry to a file) | |
