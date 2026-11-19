.. meta::
   :description: ESP32-S3 firmware that drives a TTL monochrome MDA or Hercules type monitor at 720x350 over a DE-9 connector.

.. raw:: html

   <p align="right">
     <picture>
       <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/graphics/logo/jupiter-logo-dark.svg">
       <img alt="jupiter.au" src="https://raw.githubusercontent.com/belikh/design/latest/packages/graphics/logo/jupiter-logo-light.svg" width="260">
     </picture>
   </p>

   <h1>Monochrome</h1>

Firmware for an ESP32-S3 that drives a TTL monochrome monitor (MDA or Hercules type) through a DE-9 connector. It draws 720x350 at about 50 Hz with the S3's RGB LCD peripheral and shows a calibration pattern.

`Wire it up`_ · `Build and flash`_ · `Draw`_ · `Timing`_

|

.. raw:: html

   <p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-dark.svg"><img alt="" src="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-light.svg" width="50%"></picture></p>

|

.. |monitor-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/monitor-light.svg#gh-light-mode-only
   :width: 20
.. |monitor-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/monitor-dark.svg#gh-dark-mode-only
   :width: 20
.. |sun-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/sun-light.svg#gh-light-mode-only
   :width: 20
.. |sun-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/sun-dark.svg#gh-dark-mode-only
   :width: 20
.. |pulse-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/pulse-light.svg#gh-light-mode-only
   :width: 20
.. |pulse-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/pulse-dark.svg#gh-dark-mode-only
   :width: 20
.. |signal-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/signal-light.svg#gh-light-mode-only
   :width: 20
.. |signal-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/signal-dark.svg#gh-dark-mode-only
   :width: 20
.. |minus-l| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/minus-light.svg#gh-light-mode-only
   :width: 20
.. |minus-d| image:: https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/icon/minus-dark.svg#gh-dark-mode-only
   :width: 20

Wire it up
----------

Board: ESP32-S3-DevKitC-1 with the N16R8 module (16 MB flash, 8 MB octal PSRAM).

.. list-table::
   :header-rows: 1

   * - Signal
     - GPIO
     - DE-9 pin
     - Notes
   * - |monitor-l| |monitor-d| Video
     - 4
     - 7
     - Pixel bit. 1 is on, 0 is off.
   * - |sun-l| |sun-d| Intensity
     - 5
     - 6
     - Bright pixel bit.
   * - |pulse-l| |pulse-d| H-sync
     - 6
     - 8
     - 18.4 kHz, active high.
   * - |signal-l| |signal-d| V-sync
     - 7
     - 9
     - About 50 Hz, active low.
   * - |minus-l| |minus-d| Ground
     - GND
     - 1 and 2
     - Connect both pins.

The development setup drives the monitor directly from the GPIOs, with no buffer and no resistors, and the picture is stable. If your monitor misbehaves, put a 74HCT244 or 74AHCT125 buffer, or 150 ohm series resistors, between the GPIOs and the connector.

.. raw:: html

   <table>
     <tr align="left"><th>Warning</th></tr>
     <tr><td>

Do not use GPIO 33 to 37 on an N16R8 board, because the octal PSRAM uses them. Avoid the strapping pins (0, 3, 45, 46) and the USB pins (19, 20).

.. raw:: html

   </td></tr>
   </table>

Build and flash
---------------

You need ESP-IDF 5.5.

.. code-block:: sh

   idf.py set-target esp32s3
   idf.py -p /dev/ttyACM0 flash monitor

With the video wire unplugged you should still get a steady raster. Connect video once the monitor holds lock.

Choose a demo
-------------

Set ``MODE`` in ``main/main.c`` and rebuild.

.. list-table::
   :header-rows: 1

   * - ``MODE``
     - Shows
   * - 3 (default)
     - Frames sent over Wi-Fi by TCP or UDP. Until one arrives, the monitor shows the board's IP address.
   * - 2
     - A shaded 3D torus knot with a light orbiting it, at 49 frames per second.
   * - 1
     - A wireframe icosahedron that tumbles and bounces around the screen.
   * - 0
     - A static calibration pattern with borders, a grid, intensity blocks and a checkerboard.

Send frames over Wi-Fi
----------------------

Mode 3 joins a 2.4 GHz Wi-Fi network and shows the frames that a program sends to port 5050 over TCP or UDP. Frames can be compressed with zlib.

.. code-block:: sh

   idf.py menuconfig          # Monochrome menu: network name and password
   idf.py -p /dev/ttyACM0 flash monitor
   python3 tools/send.py 192.168.2.166 --demo --udp --fmt 2bpp --fps 50 --zlib

The monitor shows the board's IP address until a frame arrives. The network settings go into ``sdkconfig``, which Git ignores. ``tools/send.py`` needs only Python 3. Frame formats and header layouts are in the frame protocol reference on docs.jupiter.au.

.. list-table::
   :header-rows: 1

   * - Format
     - Bytes per frame
     - Measured with the demo pattern
   * - 1 bit per pixel, UDP, compressed
     - 1.0 KB
     - 49.7 frames per second
   * - 2 bits per pixel, UDP, compressed
     - 1.8 KB
     - 49.7 frames per second
   * - 2 bits per pixel, UDP, not compressed
     - 62 KB
     - 40 frames per second, about one packet in five lost

The demo is a repeating pattern, so it compresses far better than a photo.

Draw
----

The frame buffer holds one byte per pixel, 720x350 pixels: ``0x00`` is black, ``0x01`` is normal and ``0x03`` is bright. To draw from the firmware, write into the frame buffer in ``test_pattern()`` (mode 0). Modes 2 and 3 keep two frame buffers in PSRAM, draw into the hidden one and flip. ``knot_run()`` in ``main/knot.c`` shows how.

.. raw:: html

   <table>
     <tr align="left"><th>Note</th></tr>
     <tr><td>

Modes 2 and 3 send the frame buffers through two 35-line bounce buffers. The number of bounce chunks per frame must be even. With 10-line chunks (35 per frame) or 70-line chunks (5 per frame) the picture bounced or showed repeated bands. Keep V-sync active low. Positive V-sync changed the picture size on the monitor this was tested on.

.. raw:: html

   </td></tr>
   </table>

.. raw:: html

   <p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-dark.svg"><img alt="" src="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-light.svg" width="50%"></picture></p>

|

Timing
------

.. list-table::
   :header-rows: 1

   * - Setting
     - Value
   * - Pixel clock
     - 16.257 MHz
   * - Horizontal
     - 720 active, 18 front porch, 108 sync, 36 back porch (882 total, 18.43 kHz)
   * - Vertical
     - 350 active, 1 front porch, 16 sync, 3 back porch (370 total, about 49.8 Hz)
   * - Sync polarity
     - H-sync active high, V-sync active low

If the picture is off centre, move pixels between the horizontal front and back porches, keeping their sum at 54. For the vertical porches, keep their sum at 4.

|

.. raw:: html

   <p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-dark.svg"><img alt="" src="https://raw.githubusercontent.com/belikh/design/latest/packages/github/graphics/rule/end-light.svg" width="50%"></picture></p>

.. raw:: html

   <p align="right"><sub>jupiter.au</sub></p>
