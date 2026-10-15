# Fichero

`fichero` is a lightweight command-line utility for the Fichero D11s thermal label printer.
It liberates the printer from restrictive smartphone apps, allowing for native operation on
Linux (and after some adaptation, it works on BSD too).
It enables seamless, scriptable batch printing, and being written entirely in pure C means zero reliance on heavy software stacks like Python or Node.js.

## Supported Printers (AiYin) - Bluetooth & USB
* Fichero D11s (from Action stores)
* AiYin D11, D11s, D11 Pro
* BEEPRT D11 Pro
* Rhebar D11s
* LuckDano D11 Pocket Label Printer
* PikDik D11 / D11 Pro

### Experimentally Supported Printers (RFID labels) - Bluetooth Only
* Niimbot D11
* Niimbot D110

## Usage
```
$ fichero
USAGE
  fichero [options]

OPTIONS
  -d, --device <path>      RFCOMM path (default: /dev/rfcomm0) or usb
  -m, --model <model>      Select printer model (1 - Fichero/AiYin D11s, 2 - Niimbot D11,D110)
  -f, --font <path>        Path to PCF font
  -t, --text <text>        Print formated text label (\a-center \f-left \r-right \b-bold toggle \n-newline)
  -b, --datamatrix <text>  Print Data Matrix encoding text value
  -c, --code128 <text>     Print Code 128 barcode
  -3, --ean13 <text>       Print EAN-13 barcode
  -8, --ean8 <text>        Print EAN-8 barcode
  -5, --ean5 <text>        Print EAN-5 barcode
  -I, --itf <text>         Print ITF barcode
  -i, --image <file>       Print image loaded from PBM (P1) file
  -T, --testpattern        Print Test Pattern
  -D, --density <level>    Set density level 0-2 (default: 1)
  -p, --paper <type>       Paper type: 0=gap, 1=black_mark, 2=continuous (default: 0)
  -s, --preview            Preview before print
  -h, --help               Show this help message

EXAMPLE
  sudo rfcomm connect 0 12:0F:F1:CE:C0:DE 1
  fichero -m1 -d /dev/rfcomm0 -D2 -s -t 'Hello World`
  fichero -m1 -d usb -s -t 'hello'
```

## Building and Installation

Build using `make`. The default installation prefix is `/usr/local/bin/` and `/usr/local/share/fichero/` (the program will look there to locate fonts at `/usr/local/share/fichero/fonts`).

```
$ make
```

Then install to these directories:

```
$ make install
```

You can also build and install locally in your home directory (e.g., `~/.local`) using the following command if you want the program to be available only for you locally and not system wide:
```
$ make PREFIX=~/.local install
```

### Dependencies

Requires pkg-config and development libraries for USB communication and DataMatrix code
```
apt install pkg-config libusb-1.0-0-dev libdmtx-dev
```

# Label Design and Printing Manual

## 1. Text Labels & Formatting

When printing text labels using the `-t` option, `fichero` supports inline formatting tags and escape sequences. This allows you to align text, insert multi-line blocks, apply bold weight, and use special characters.

### Available Formatting Tags:
* `\f` — Left align text (default)
* `\a` — Center align text
* `\r` — Right align text
* `\b` — Toggle bold font style (renders characters with a 1-pixel horizontal offset)
* `\n` — Insert a newline (supports up to 3 text lines per label)

### Example Usage:
```bash
fichero -m1 -s -t '\aHEADER LINE\n\bImportant: Item #123\n\fLeft-aligned note'
```

---

## 2. Printing Data Matrix Codes

You can automatically generate and print 2D Data Matrix barcodes directly from string input using the `-b` option:

```bash
fichero -m1 -s -b 'SKU-987654321'
```

The program uses `libdmtx` to encode the provided text into a square symbol, automatically centers it on the label, and renders it crisp and clear.

---

## 3. Printing Custom Bitmaps (PBM Format)

If you need precise graphical control, logos, icons, or custom layouts that go beyond standard fonts, `fichero` supports the **Portable BitMap (PBM)** format (specifically plaintext **P1** format).

### PBM P1 File Structure
A PBM file is a plain text file containing:
1. Magic number: `P1`
2. Dimensions: Width and height in pixels (width must be `96` pixels to match the printhead width).
3. Pixel data: `0` for white (background), `1` for black (dot printed).

Example of a tiny 96x3 PBM file:
```pbm
P1
# This is a comment
96 3
0 0 0 0 ... (96 values per row)
1 1 1 1 ...
0 0 0 0 ...
```

### Generating PBM Files Programmatically or via Scripts
You can easily generate PBM files using Python, ImageMagick, or shell scripts.

Using ImageMagick to convert an image or generate text:
```bash
convert -size 96x224 xc:white -font Roboto -pointsize 20 -draw "text 10,50 'Hello'" pbm:- > label.pbm
```
Then print it using `fichero`:
```bash
fichero -m1 -s -i label.pbm
```

---

## Custom Fonts

The program supports the standard bitmap font format PCF (Portable Compiled Format), which you may know from the X Window System (X11). This format is directly related to BDF (Bitmap Distribution Format), allowing you to easily convert between them using standard tools like `bdftopcf`.

Fichero label printing program supports PCF fonts in both ASCII and Unicode (text labels are written using the widely used UTF-8 encoding), enabling you to print labels in practically any language depending on your prepared font set.

To convert TrueType (TTF) and OpenType (OTF) fonts, you can use `otf2bdf`, and for text-format `bdf` fonts, the `bdftopcf` tool will be helpful.

For example, converting TTF and OTF fonts for Polish characters can be done like this:
```
otf2bdf -pid 3 -eid 1 -p 18 -l "32_126 160_255 256_383" -o RobotoCondensed-Regular.bdf RobotoCondensed-Regular.ttf
bdftopcf -o RobotoCondensed-Regular.pcf RobotoCondensed-Regular.bdf
```

With the generated sample font `RobotoCondensed-Regular.pcf`, you can immediately preview a centered label text like "Abcd-01234" (this feature requires a terminal at least 100 characters wide). The label is rendered top-to-bottom, meaning the text flows from left to right vertically. The width of your terminal represents the 96-pixel label height, and the full length of the printout can be viewed by scrolling through the terminal history.

```bash
$ fichero -m1 -s -f RobotoCondensed-Regular.pcf -t '\aAbcd-01234'
```

To create or edit your own fonts, you can use the simple `gbdfed` program (GTK-based BDF font editor) available in every Linux distribution, or the more advanced `fontforge`. Fonts created with `gbdfed` are extremely crisp and sharp after printing.

## Printer Connected via Bluetooth

To connect the printer, you should know its MAC address. In the standard (non-BLE) profile, our placeholder MAC address is `12:0F:F1:CE:C0:DE` (12 Off1ce C0de). Naturally, your correct MAC address should be written in hexadecimal format; find it by scanning for nearby Bluetooth devices.

Once you have your printer's MAC address, to connect to it immediately after powering it on, run the following command in a separate terminal:
```bash
sudo rfcomm connect 0 12:0F:F1:CE:C0:DE 1
```

Upon successful connection, the terminal will show:
```
Connected /dev/rfcomm0 to 12:0F:F1:CE:C0:DE on channel 1
Press CTRL-C for hangup
```
Leave this terminal open during printing. If the printer automatically shuts down due to inactivity or is turned off, the connection will drop.

Remember the device path `/dev/rfcomm0` (or similar); it will be used to connect to the printer. By default, it is `/dev/rfcomm0`.

## AiYin (Fichero) D11/D11s/D11Pro Connected via USB

After connecting the printer via a USB cable and powering it on, use `lsusb` to find its VID and PID.
```bash
$ sudo lsusb
...
Bus 003 Device 017: ID 09c5:0200 Memory Corp. Printer
...
```

Meanwhile, the `dmesg` tool will show that the system likely tried to attach the `usblp` module for this device:

```
[1619441.731663] usb 3-1: new full-speed USB device number 18 using xhci_hcd
[1619441.950673] usb 3-1: New USB device found, idVendor=09c5, idProduct=0200, bcdDevice= 2.00
[1619441.950702] usb 3-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
[1619441.950714] usb 3-1: Product: Printer
[1619441.950723] usb 3-1: Manufacturer: Printer
[1619441.950730] usb 3-1: SerialNumber: AA0FF1CEC0DE
[161945.827] usblp 3-1:1.0: usblp0: USB Bidirectional printer dev 18 if 0 alt 0 proto 2 vid 0x09C5 pid 0x0200
[1619447.088724] usblp0: removed
[1619447.092846] usblp 3-1:1.0: usblp0: USB Bidirectional printer dev 18 if 0 alt 0 proto 2 vid 0x09C5 pid 0x0200
[1619618.517971] usblp0: removed
```

We will not use `usblp` for communication with this printer. Therefore, it is recommended to add an appropriate udev rule to prevent the system from automatically binding the `usblp` driver. To do this, create a file named `/etc/udev/rules.d/99-aiyin.rules` and add the following content:

```udev
# If the device is your printer (by VID and PID), clear the assigned usblp driver
SUBSYSTEM=="usb", ATTRS{idVendor}=="09c5", ATTRS{idProduct}=="0200", ENV{ID_USB_DRIVER}=""

# At the same time, grant user permissions to LibUSB (to work without sudo)
SUBSYSTEM=="usb", ATTRS{idVendor}=="09c5", ATTRS{idProduct}=="0200", MODE="0666", GROUP="plugdev"
```

Then reload the udev configuration:

```bash
$ sudo udevadm control --reload-rules && sudo udevadm trigger
```

This ensures you won't need to run the printer utility with `sudo`.
