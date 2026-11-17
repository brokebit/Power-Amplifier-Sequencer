# Nextion EME sequencer dashboard

This 480 × 320 landscape dashboard targets the Nextion Basic NX4832T035_011.
It displays sequencer mode, fault status, forward and reflected power, SWR,
two PA temperatures, six relay outputs, and elapsed time in the current state.
The screen is a read-only monitor.

## Files

| File | Use |
| --- | --- |
| [KC9QLO_EME_Sequencer.HMI](KC9QLO_EME_Sequencer.HMI) | Editable Nextion Editor project with both backgrounds. KC9QLO is selected by default. |
| [KC9QLO TFT](KC9QLO_EME_Sequencer.tft) | Compiled dashboard for KC9QLO, the original sequencer creator. |
| [K9KJ TFT](K9KJ_EME_Sequencer_Color.tft) | Compiled dashboard for K9KJ. |
| [KC9QLO BMP](KC9QLO_EME_Sequencer_NX4832T035_011_480x320.bmp) and [K9KJ BMP](K9KJ_EME_Sequencer_NX4832T035_011_480x320.bmp) | Uncompressed 24-bit artwork for editing or importing into the HMI. |

## Install and connect

1. Use an ESP32-S3 firmware build from this repository. The
   [deployment guide](../README_DEPLOYMENT.md) covers ESP32 build and upload.
2. Copy one TFT to the root of a FAT32 microSD card. With the Nextion powered
   off, insert the card and turn the screen on. After the update completes,
   turn it off, remove the card, and turn it back on.
3. Connect ESP32 GPIO 17 (UART1 TX) to Nextion RX and connect the grounds.
   Supply the Nextion with regulated 5 V. The Nextion TX wire can be left
   disconnected. The UART settings are 9600 baud, 8 data bits, no parity,
   and one stop bit.

Forward power and RX are green. Reflected power and TX are red. The active
stage outline matches the mode color. PRE-TX and POST-TX use amber. A fault
appears in red in the system status panel. R1–R6 follow the physical relay
output numbers. Compare the artwork labels with your configured relay wiring.

PA temperatures come from the sequencer's thermistor inputs. On hardware
v0.2, TEMP 1 uses J2 and TEMP 2 uses J6. Each expects a 100 kΩ NTC probe
(β = 3950) between pin 2 (signal) and pin 1 (ground). An open input can read
as extreme cold. Confirm both probes give plausible readings before relying
on the overtemperature cutoff.

## Edit and rebuild

Open the HMI in Nextion Editor (built with 1.68.2). Keep the model set to
Basic / NX4832T035 and the orientation set to 480 × 320 landscape. To swap
artwork, replace picture resource **0** with the corresponding BMP. The page,
cropped text fields, stage outlines, relay indicators, and direct temperature
and timer drawing all use picture 0. Keep its dimensions and panel locations
aligned with the original. Use **Debug** for a simulator check, then
**File → TFT file output** to export.

The ESP32 [display driver](../src/nextion_display.c) sends mode, fault and RF
readings to `tState`, `tFault`, `tFwd`, `tRef`, and `tSwr`. Keep those names
on the startup page. If you move the relay, temperature, or elapsed-time
panels, update the coordinates in the driver as well.
