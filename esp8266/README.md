Micro Python port to ESP8266
============================

This is a port of Micro Python to the Espressif ESP8266 wifi module.

Currently implemented features include:
- REPL (Python prompt) over UART0.
- 24k heap RAM available for Python code.
- Garbage collector, exceptions.
- Unicode support.
- Builtin modules: gc, array, collections, io, struct, sys.
- C long-long type used as bignum implementation (gives 64 signed ints).

Note that floating-point numbers are not supported.

On the TODO list:
- Wifi support.
- GPIO support.
- Internal filesystem using the flash.
- ...

Build instructions
------------------

The tool chain required for the build is the OpenSource ESP SDK, which can be
found at <https://github.com/pfalcon/esp-open-sdk>.  Clone this repository and
run `make` in it's directory to build and install the SDK locally.  T

Then, to build Micro Python for the ESP8266, just run:
```bash
$ make
```
This should produce binary images in the `build/` subdirectory.  To flash them
to your ESP8266, use:
```bash
$ make deploy
```
This will use the `esptool.py` script to download the images.  You must have
your ESP module in the bootloader, and connected to a serial port on your PC.
The default serial port is `/dev/ttyACM0`.  To specify another, use, eg:
```bash
$ make PORT=/dev/ttyUSB0 deploy
```

The images that are built are:
- `firmware.elf-0x00000.bin`: to be flashed at 0x00000
- `firmware.elf-0x10000.bin`: to be flashed at 0x10000

There is also a combined image, made up of the above 2 binary files with the
appropriate padding:
- `firmware-combined.bin`: to be flashed at 0x00000
