# Setting up Omaputer with an AI agent

For an AI agent asked to install or set up Omaputer, this launcher, on an M5Stack Cardputer or Cardputer ADV. Work through the steps in order. Steps marked **(ask)** need the user's yes first. The [README](README.md) explains the same steps for people.

## Rules

1. **Secrets stay secret.** Never print, log, commit or send the xAI key or a WiFi password anywhere except to the Cardputer over its USB serial port. Read the key from the first line of a file the user names, and don't read the rest of that file. Don't run `settings get` on the USB console: it prints the saved WiFi passwords.
2. **Know which Cardputer you're on.** Read its MAC address (step 1) before every flash. If the user has more than one, confirm which one this is.
3. **Back up before the first install** (step 2). Keep backups outside the repository: they hold the saved WiFi passwords.
4. **Erase only once, and only with a yes.** The first install writes the whole flash and erases installed apps and settings **(ask)**. Updates write only the launcher, at `0x10000`, which keeps them.
5. **Don't restart the Cardputer by accident.** Opening its serial port with DTR and RTS low restarts it; pyserial's defaults (`serial.Serial(port, 115200)`) don't.
6. **Don't commit or push** to this repository unless the user asks.

## 1. Find the Cardputer

Plug it in by USB. Its serial port is `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem…` on macOS and `COM…` on Windows.

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 chip-id
```

The output's `MAC:` line identifies the Cardputer; note it. esptool comes from `pip install esptool`, or use PlatformIO's copy: `pio pkg exec -p tool-esptool -- esptool …`.

If esptool can't connect, ask the user to unplug the Cardputer, hold its **G0** button, plug it in and let go.

## 2. Back up (first install only)

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x800000 ~/cardputer-backup-<MAC>.bin
```

This takes a few minutes. Check that the file is exactly 8388608 bytes.

## 3. Build

From the repository's root, on the `omarchy` branch:

```sh
pio run -e m5stack-cardputer
```

It writes `firmware.factory.bin` (the whole image) and `firmware.bin` (the launcher alone) to `.pio/build/m5stack-cardputer/`.

## 4. Flash

Read the MAC again (step 1), then:

- **First install (ask):** erases everything on the Cardputer.

  ```sh
  esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0x0 .pio/build/m5stack-cardputer/firmware.factory.bin
  ```

- **Update:** keeps the apps and settings.

  ```sh
  esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0x10000 .pio/build/m5stack-cardputer/firmware.bin
  ```

Both end with `Hash of data verified.`, and esptool restarts the Cardputer afterwards.

## 5. Check that it started

Open the serial port at 115200 baud with pyserial's defaults. While the boot screen shows, it prints `Press the button to enter the Launcher!` every half second; send `nav SelPress` then to open the menu. Then:

- `partitions` lists `app0` with `size=0x1B0000 (1728Kb)`: this edition's layout.
- `help` lists every command.

## 6. WiFi

Ask the user for the network's name and password, or for a file that holds them. Send these without echoing the password:

| Send | Expect |
| --- | --- |
| `wifi add "<network>" "<password>"` | `OK saved <network>` |
| `wifi auto` | `OK connected to <network>, ip=<address>` |

From then on the Cardputer joins that network at every start, and its log shows `WiFi at launch: joined <network>, ip <address>`.

## 7. The xAI key, for Grok

Ask which file holds it, then send it from that file's first line without printing it. For example:

```python
import serial, time

key = open(KEY_FILE, encoding="utf-8").readline().strip()  # the first line only
port = serial.Serial("/dev/ttyACM0", 115200, timeout=1)     # pyserial's defaults: no restart
time.sleep(0.5)
port.reset_input_buffer()
port.write(f"xai-key {key}\n".encode())
del key
time.sleep(1)
reply = port.read(4096).decode(errors="replace")
print("saved" if "OK xAI key saved" in reply else "not saved")
```

Report only whether it was saved.

## 8. Hand over

Tell the user:

- To open the web console, choose **Web UI → my Network** on the Cardputer, then open the address it shows: `http://cardputer.local`, or `http://cardputer-adv.local` on an ADV.
- The default login is `admin` / `launcher`. Ask them to change it under **5 settings → portal login**.
- Grok's voice and chat are billed to their xAI account.

## Troubleshooting

- **esptool can't connect:** use download mode (G0 held while plugging in), try another USB cable (some only charge), and close anything else holding the port.
- **There's no serial port at all:** the Cardputer is off, the cable only charges, or the Cardputer is in USB drive mode; press `` ` `` on it to leave that.
- **The serial port vanishes:** the Cardputer is restarting. Open it again after a second.
- **`WiFi at launch: try 1 of 3 to join <network> refused`:** right after a restart the router can turn the first try away. The Cardputer tries three times.
- **The chat says the Cardputer isn't on the internet:** Web UI is running on the Cardputer's own network (AP mode). Use **my Network**.
