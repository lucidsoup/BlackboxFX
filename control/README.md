# BlackboxFX Control

A browser app that turns the stock M-VAVE ANN BlackBox into a moving,
morphing effects box over **USB or Bluetooth**, with no firmware changes.

- **Signal chain:** every block's model, on/off and knobs, live. Reorder the
  chain with the arrows. Amp and cab names are read from your pedal.
- **Movement:** lanes that animate any knob: sine, triangle, ramps, square,
  stepped random or smooth drift. Run free in Hz or synced to tempo (tap or
  type BPM). For example, put Reverb Mix on drift, or bend Delay Time with a
  slow sine.
- **Morph:** capture two sounds and sweep between them with one slider.
- **Scenes:** eight snapshots to recall instantly. Export/import as JSON.

## Safety

Everything is a **live change** to the sound you're playing. The app has
no code for saving to the pedal's memory or touching firmware; reloading the
preset on the pedal undoes it all. Until the app has read the pedal's current
sound it sends nothing, so connecting never changes your sound.

## Run it

Web MIDI and Web Bluetooth need **Chrome or Edge** (desktop or Android) and a
secure page, which `localhost` counts as:

```bash
cd control
python3 -m http.server 8000
# open http://localhost:8000
```

- **USB:** plug the BlackBox in, click USB, allow MIDI/SysEx access. If the
  pedal's port isn't recognised by name, a dropdown lets you pick it.
- **Bluetooth:** click Bluetooth and pick `BlackBox_BLE`.
- **Simulator:** try everything without a pedal.

Safari and Firefox lack Web MIDI with SysEx and Web Bluetooth, so they can
only run the simulator.

## Tests

```bash
npm test          # protocol + logic (Node 18+)
npm run test:e2e  # whole UI in headless Chromium against the simulator (needs playwright)
```

## Credits

Protocol facts: [jvsobrinho/mvave-blackbox-ble](https://github.com/jvsobrinho/mvave-blackbox-ble)
(CC BY-SA 4.0). Connection approach cross-checked against
[jvsobrinho/mvave-blackbox-edit](https://github.com/jvsobrinho/mvave-blackbox-edit).
This is an independent implementation, not affiliated with M-VAVE.
