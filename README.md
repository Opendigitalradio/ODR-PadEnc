ODR-PadEnc
==========

ODR-PadEnc is an encoder for Programme Associated Data (PAD) and includes
support for:

* MOT Slideshow (including catSLS), according to ETSI EN 301 234 and TS 101 499
* DLS (including DL Plus), according to ETSI EN 300 401 and TS 102 980

To encode DLS and Slideshow data, the `odr-padenc` tool reads images
from a folder and DLS text from a file, and generates the PAD data
for the encoder.

For detailed usage, see the usage screen of the tool with the `-h` option.

More information is available on the
[Opendigitalradio wiki](http://wiki.opendigitalradio.org/ODR-PadEnc)

How to build
=============

Requirements:

* a C++11 compiler
* ImageMagick MagickWand (optional, for MOT Slideshow),
  version 6 (legacy) or 7

To install MagickWand on Debian or Ubuntu:

    sudo apt-get install libmagickwand-dev

To compile and install ODR-PadEnc:

    ./bootstrap
    ./configure
    make
    sudo make install

ImageMagick and Debian Jessie/Ubuntu 16.04
------------------------------------------
Please note that Debian Jessie and Ubuntu 16.04 shipped a version of
ImageMagick that was affected by two memory leaks (see #2, and also
Ubuntu's [#1671630](https://bugs.launchpad.net/ubuntu/+source/imagemagick/+bug/1671630) and [#1680543](https://bugs.launchpad.net/debian/+source/imagemagick/+bug/1680543)). Hence ODR-PadEnc increasingly consumed
memory until it crashed.

This has been fixed with the following package versions:
- Debian Jessie: 8:6.8.9.9-5+deb8u9
- Ubuntu 16.04: 8:6.8.9.9-7ubuntu5.7

Usage of MOT Slideshow and DLS
==============================

`odr-padenc` reads images from the specified folder, and generates the PAD
data for the encoder. This is communicated through a socket to the encoder. It
also reads DLS from a file, and includes this information in the PAD.

If ImageMagick is available
---------------------------
It can read all file formats supported by ImageMagick, and by default resizes
them to 320x240 pixels, and compresses them as JPEG. If the input file is already
a JPEG file of the correct size, and smaller than 50kB, it is sent without further
compression. If the input file is a PNG that satisfies the same criteria, it is
transmitted as PNG without any recompression.

RAW Format
----------
If ImageMagick is not compiled in, or when enabled with the `-R` option, the images
are not modified, and are transmitted as-is. Use this if you can guarantee that
the generated files are smaller than 50kB and not larger than 320x240 pixels.
Files whose name end in `_PadEncRawMode.png` or `_PadEncRawMode.jpg` will always
be transmitted in RAW mode.
It may be useful to apply [`optipng`](http://optipng.sourceforge.net) to any PNG file prior to transmission.

Supported Encoders
------------------
`odr-audioenc` and `odr-sourcecompanion` can insert the PAD data from `odr-padenc` into the bitstream.

This is an ongoing development.  Only some PAD lengths are supported,
please see `odr-padenc`'s help.

ODR-PadEnc v2 is compatible with ODR-AudioEnc v2 and ODR-SourceCompanion v0.x, and uses a fifo to communicate between
the tools.

ODR-PadEnc v3 replaced the fifo with a socket and is compatible with ODR-AudioEnc v3 and ODR-SourceCompanion v1.

Character Sets
--------------
When `odr-padenc` is launched with the default character set options, it assumes
that the DLS text in the file is encoded in UTF-8, and will convert it according to
the DAB standard to the *Complete EBU Latin based repertoire* character set encoding.

If you set the character set encoding to any other setting (except
*Complete EBU Latin based repertoire* which needs no conversion),
`odr-padenc` will abort, as it does not support any other conversion than from
UTF-8 to *Complete EBU Latin based repertoire*.

You can however use the `-C` option to transmit the untouched DLS text. In this
case, it is your responsibility to ensure the encoding is valid.  For instance,
if your data is already encoded in *Complete EBU Latin based repertoire*, you
must specify both `--charset=0` and `--raw-dls`.

Known Limitations
=================
Some receivers are unable to decode slides larger than some size, even within the allowed
size limit given in the specification.

Thanks
======

This encoder was initially called `mot-encoder` and has been contributed by
[CSP](http://rd.csp.it).
