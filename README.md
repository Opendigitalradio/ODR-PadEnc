ODR-PadEnc
==========

ODR-PadEnc is an encoder for Programme Associated Data (PAD) and includes
support for:

* MOT Slideshow (including catSLS), according to ETSI EN 301 234 and TS 101 499
* DLS (including DL Plus), according to ETSI EN 300 401 and TS 102 980

To encode DLS and Slideshow data, the *odr-padenc* tool reads images
from a folder and DLS text from a file, and generates the PAD data
for the encoder.

For detailed usage, see the usage screen of the tool with the *-h* option.

More information is available on the
[Opendigitalradio wiki](http://wiki.opendigitalradio.org/ODR-PadEnc)

How to build
=============

Requirements:

* A C++11 compiler
* ImageMagick MagickWand (optional, for MOT Slideshow)

This package:

    ./bootstrap
    ./configure
    make
    sudo make install

ImageMagick and Ubuntu 16.04
----------------------------
Please note that Ubuntu 16.04 still (09.03.2017) ships a version of ImageMagick
that was affected by a memory leak (see #2). Hence ODR-PadEnc will increasingly
consume memory until it crashes!

Until Ubuntu backports the bugfix, you should therefore use a more recent
version of ImageMagick (6.9.2-2 or higher). 

Usage of MOT Slideshow and DLS
==============================

*odr-padenc* reads images from the specified folder, and generates the PAD
data for the encoder. This is communicated through a fifo to the encoder. It
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
If ImageMagick is not compiled in, or when enabled with the -R option, the images
are not modified, and are transmitted as-is. Use this if you can guarantee that
the generated files are smaller than 50kB and not larger than 320x240 pixels.

Supported Encoders
------------------
*odr-audioenc* can insert the PAD data from *odr-padenc* into the bitstream.
The mp2 encoder [Toolame-DAB](https://github.com/Opendigitalradio/toolame-dab)
can also read *odr-padenc* data.

This is an ongoing development. Make sure you use the same pad length option
for *odr-padenc* and the audio encoder. Only some pad lengths are supported,
please see *odr-padenc*'s help.

Character Sets
--------------
When *odr-padenc* is launched with the default character set options, it assumes
that the DLS text in the file is encoded in UTF-8, and will convert it according to
the DAB standard to the *Complete EBU Latin based repertoire* character set encoding.

If you set the character set encoding to any other setting (except
*Complete EBU Latin based repertoire* which needs no conversion),
*odr-padenc* will abort, as it does not support any other conversion than from
UTF-8 to *Complete EBU Latin based repertoire*.

You can however use the -C option to transmit the untouched DLS text. In this
case, it is your responsibility to ensure the encoding is valid.  For instance,
if your data is already encoded in *Complete EBU Latin based repertoire*, you
must specify both --charset=0 and --raw-dls.

Known Limitations
=================
*odr-padenc* encodes slides in a 10 second interval, which is not linked
to the rate at which the encoder reads the PAD data. It also doesn't prioritise
DLS transmission over Slides.

Some receivers are unable to decode slides larger than some size, even within the allowed
size limit given in the specification.

Thanks
======

This encoder was initially called *mot-encoder* and has been contributed by
[CSP](http://rd.csp.it).
