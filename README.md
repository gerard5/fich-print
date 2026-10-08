# Fichero

`fichero` is a lightweight command-line utility for the Fichero D11s thermal label printer.
It liberates the printer from restrictive smartphone apps, allowing for native operation on
Linux (and after some adaptation, it works on BSD too).
It enables seamless, scriptable batch printing, and being written entirely in pure C means zero reliance on heavy software stacks like Python or Node.js.

## Usage
```
USAGE
  ./fichero [options]

OPTIONS
  -d <dev/path> RFCOMM device path (default: /dev/rfcomm0)
  -i <file>     Load input from PBM (P1) file
  -D <density>  Set density 0-2 (default: 1)
  -p <type>     Paper type: 0=gap, 1=black_mark, 2=continuous (default: 0)
  -t <text>     Label text (format \a-center \f-left \r-right \b-bold toggle)
  -f <font>     path to bdf font
  -b <text>     Generate datamatrix code
  -s            Preview before print
  -h            Show this help message

EXAMPLE
  sudo rfcomm connect 0 12:0F:F1:CE:C0:DE 1
  ./fichero -d /dev/rfcomm0 -D 2 -s -t 'hello'
```

## Dependencies

DataMatrix 2D code requires external library to be installed
```
apt install libdmtx0b libdmtx-dev
```

## TODO
* barcode support (EAN13, EAN5 and CODE128)
* use PCF (binary) fonts instead of BDF (fonts)
* add USB support
* add Niimbot D11, D110 support
