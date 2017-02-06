/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014, 2015 Matthias P. Braendli (http://opendigitalradio.org)

    Copyright (C) 2015, 2016, 2017 Stefan Pöschel (http://opendigitalradio.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
    \file odr-padenc.cpp
    \brief Generete PAD data for MOT Slideshow and DLS

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "common.h"

#include <stdlib.h>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <list>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <getopt.h>

#include "charset.h"
#include "pad_common.h"

#if HAVE_MAGICKWAND
#  include <wand/magick_wand.h>
#endif



#define SLEEPDELAY_DEFAULT 10       // seconds

#define XSTR(x) #x
#define STR(x) XSTR(x)

static const size_t MAXDLS          =   128; // chars
static const size_t MAXSEGLEN       =  8189; // Bytes (EN 301 234 v2.1.1, ch. 5.1.1)
static const size_t MAXSLIDESIZE    = 51200; // Bytes (TS 101 499 v3.1.1, ch. 9.1.2)

static const int    MAXSLIDEID      =  9999; // Roll-over value for fidx
static const size_t MAXHISTORYLEN   =    50; // How many slides to keep in history
static const int    MINQUALITY      =    40; // Do not allow the image compressor to go below JPEG quality 40

// Charsets from TS 101 756
enum {
    CHARSET_COMPLETE_EBU_LATIN      =  0, //!< Complete EBU Latin based repertoire
    CHARSET_EBU_LATIN_CY_GR         =  1, //!< EBU Latin based common core, Cyrillic, Greek
    CHARSET_EBU_LATIN_AR_HE_CY_GR   =  2, //!< EBU Latin based core, Arabic, Hebrew, Cyrillic and Greek
    CHARSET_ISO_LATIN_ALPHABET_2    =  3, //!< ISO Latin Alphabet No 2
    CHARSET_UCS2_BE                 =  6, //!< ISO/IEC 10646 using UCS-2 transformation format, big endian byte order
    CHARSET_UTF8                    = 15  //!< ISO Latin Alphabet No 2
};

int verbose = 0;

struct MSCDG {
    // MSC Data Group Header (extension field not supported)
    unsigned char extflag;      //  1 bit
    unsigned char crcflag;      //  1 bit
    unsigned char segflag;      //  1 bit
    unsigned char accflag;      //  1 bit
    unsigned char dgtype;       //  4 bits
    unsigned char cindex;       //  4 bits
    unsigned char rindex;       //  4 bits
    /// Session header - Segment field
    unsigned char last;         //  1 bit
    unsigned short int segnum;  // 16 bits
    // Session header - User access field
    unsigned char rfa;          //  3 bits
    unsigned char tidflag;      //  1 bit
    unsigned char lenid;        //  4 bits - Fixed to value 2 in this implemntation
    unsigned short int tid;     // 16 bits
    // MSC data group data field
    //  Mot Segmentation header
    unsigned char rcount;       //  3 bits
    unsigned short int seglen;  // 13 bits
    // Mot segment
    unsigned char* segdata;
    // MSC data group CRC
    unsigned short int crc;     // 16 bits
};

/*! Between collection of slides and transmission, the slide data is saved
 * in this structure.
 */
struct slide_metadata_t {
    // complete path to slide
    std::string filepath;

    // index, values from 0 to MAXSLIDEID, rolls over
    int fidx;

    // This is used to define the order in which several discovered
    // slides are transmitted
    bool operator<(const slide_metadata_t& other) const {
        return this->fidx < other.fidx;
    }
};

/*! A simple fingerprint for each slide transmitted.
 * Allows us to reuse the same fidx if the same slide
 * is transmitted more than once.
 */
struct fingerprint_t {
    // file name
    std::string s_name;
    // file size, in bytes
    off_t s_size;
    // time of last modification
    unsigned long s_mtime;

    // assigned fidx, -1 means invalid
    int fidx;

    /*! The comparison is not done on fidx, only
     * on the file-specific data
     */
    bool operator==(const fingerprint_t& other) const {
        return (((s_name == other.s_name &&
                 s_size == other.s_size) &&
                s_mtime == other.s_mtime));
    }

    void disp(void) {
        printf("%s_%ld_%lu:%d\n", s_name.c_str(), s_size, s_mtime, fidx);
    }

    void load_from_file(const char* filepath)
    {
        struct stat file_attribue;
        const char * final_slash;

        stat(filepath, &file_attribue);
        final_slash = strrchr(filepath, '/');

        // load filename, size and mtime
        // Save only the basename of the filepath
        this->s_name.assign((final_slash == NULL) ? filepath : final_slash + 1);
        this->s_size = file_attribue.st_size;
        this->s_mtime = file_attribue.st_mtime;

        this->fidx = -1;
    }
};

/*! We keep track of transmitted files so that we can retransmit
 * identical slides with the same index, in case the receivers cache
 * them.
 *
 * \c MAXHISTORYLEN defines for how how many slides we want to keep this
 * history.
 */
class History {
    public:
        History(size_t hist_size) :
            m_hist_size(hist_size),
            m_last_given_fidx(0) {}
        void disp_database();
        // controller of id base on database
        int get_fidx(const char* filepath);

    private:
        std::deque<fingerprint_t> m_database;

        size_t m_hist_size;

        int m_last_given_fidx;

        // find the fingerprint fp in database.
        // returns the fidx when found,
        //    or   -1 if not found
        int find(const fingerprint_t& fp) const;

        // add a new fingerprint into database
        // returns its fidx
        void add(fingerprint_t& fp);
};


int encodeFile(int output_fd, const std::string& fname, int fidx, bool raw_slides);

uint8_vector_t createMotHeader(
        size_t blobsize,
        int fidx,
        bool jfif_not_png,
        const std::string &params_fname);

void createMscDG(MSCDG* msc, unsigned short int dgtype, int *cindex, unsigned short int segnum,
        unsigned short int lastseg, unsigned short int tid, unsigned char* data,
        unsigned short int datalen);

DATA_GROUP* packMscDG(MSCDG* msc);

struct DL_STATE;
void prepend_dl_dgs(const DL_STATE& dl_state, uint8_t charset);
void writeDLS(int output_fd, const std::string& dls_file, uint8_t charset, bool raw_dls, bool remove_dls);


// MOT Slideshow related
static int cindex_header = 0;
static int cindex_body = 0;

static const std::string SLS_PARAMS_SUFFIX = ".sls_params";


class MOTHeader {
private:
    size_t header_size;
    uint8_vector_t data;

    void IncrementHeaderSize(size_t size);
    void AddParamHeader(int pli, int param_id) {data.push_back((pli << 6) | (param_id & 0x3F));}

    void AddExtensionFixedSize(int pli, int param_id, const uint8_t* data_field, size_t data_field_len);
    void AddExtensionVarSize(int param_id, const uint8_t* data_field, size_t data_field_len);
public:
    MOTHeader(size_t body_size, int content_type, int content_subtype);

    void AddExtension(int param_id, const uint8_t* data_field, size_t data_field_len);
    const uint8_vector_t GetData() {return data;}
};

MOTHeader::MOTHeader(size_t body_size, int content_type, int content_subtype)
: header_size(0), data(uint8_vector_t(7, 0x00)) {
    // init header core

    // body size
    data[0] = (body_size >> 20) & 0xFF;
    data[1] = (body_size >> 12) & 0xFF;
    data[2] = (body_size >>  4) & 0xFF;
    data[3] = (body_size <<  4) & 0xF0;

    // header size
    IncrementHeaderSize(data.size());

    // content type
    data[5] |= (content_type << 1) & 0x7E;

    // content subtype
    data[5] |= (content_subtype >> 8) & 0x01;
    data[6] |=  content_subtype       & 0xFF;
}

void MOTHeader::IncrementHeaderSize(size_t size) {
    header_size += size;

    data[3] &= 0xF0;
    data[3] |= (header_size >> 9) & 0x0F;

    data[4]  = (header_size >> 1) & 0xFF;

    data[5] &= 0x7F;
    data[5] |= (header_size << 7) & 0x80;
}

void MOTHeader::AddExtensionFixedSize(int pli, int param_id, const uint8_t* data_field, size_t data_field_len) {
	AddParamHeader(pli, param_id);

	for (size_t i = 0; i < data_field_len; i++)
		data.push_back(data_field[i]);

	IncrementHeaderSize(1 + data_field_len);
}

void MOTHeader::AddExtensionVarSize(int param_id, const uint8_t* data_field, size_t data_field_len) {
    AddParamHeader(0b11, param_id);

    // longer field lens use 15 instead of 7 bits
    bool ext = data_field_len > 127;
    if (ext) {
        data.push_back(0x80 | ((data_field_len >> 8) & 0x7F));
        data.push_back(data_field_len & 0xFF);
    } else {
        data.push_back(data_field_len & 0x7F);
    }

    for (size_t i = 0; i < data_field_len; i++)
        data.push_back(data_field[i]);

    IncrementHeaderSize(1 + (ext ? 2 : 1) + data_field_len);
}

void MOTHeader::AddExtension(int param_id, const uint8_t* data_field, size_t data_field_len) {
	int pli;

	switch(data_field_len) {
	case 0:
		pli = 0b00;
		break;
	case 1:
		pli = 0b01;
		break;
	case 4:
		pli = 0b10;
		break;
	default:
		pli = 0b11;
		break;
	}

	if (pli == 0b11)
		AddExtensionVarSize(param_id, data_field, data_field_len);
	else
		AddExtensionFixedSize(pli, param_id, data_field, data_field_len);
}



// DLS related
static const size_t DLS_SEG_LEN_PREFIX = 2;
static const size_t DLS_SEG_LEN_CHAR_MAX = 16;
enum {
    DLS_CMD_REMOVE_LABEL = 0b0001,
    DLS_CMD_DL_PLUS      = 0b0010
};
static const int DL_PLUS_CMD_TAGS = 0b0000;

#define DL_PARAMS_OPEN          "##### parameters { #####"
#define DL_PARAMS_CLOSE         "##### parameters } #####"

static CharsetConverter charset_converter;

struct DL_PLUS_TAG {
    int content_type;
    int start_marker;
    int length_marker;

    DL_PLUS_TAG() :
        content_type(0),    // = DUMMY
        start_marker(0),
        length_marker(0)
    {}

    DL_PLUS_TAG(int content_type, int start_marker, int length_marker) :
        content_type(content_type),
        start_marker(start_marker),
        length_marker(length_marker)
    {}

    bool operator==(const DL_PLUS_TAG& other) const {
        return
            content_type == other.content_type &&
            start_marker == other.start_marker &&
            length_marker == other.length_marker;
    }
    bool operator!=(const DL_PLUS_TAG& other) const {
        return !(*this == other);
    }
};

typedef std::vector<DL_PLUS_TAG> dl_plus_tags_t;

struct DL_STATE {
    std::string dl_text;

    bool dl_plus_enabled;
    bool dl_plus_item_toggle;
    bool dl_plus_item_running;
    dl_plus_tags_t dl_plus_tags;

    DL_STATE() :
        dl_plus_enabled(false),
        dl_plus_item_toggle(false),
        dl_plus_item_running(false)
    {}

    bool operator==(const DL_STATE& other) const {
        if (dl_text != other.dl_text)
            return false;
        if (dl_plus_enabled != other.dl_plus_enabled)
            return false;
        if (dl_plus_enabled) {
            if (dl_plus_item_toggle != other.dl_plus_item_toggle)
                return false;
            if (dl_plus_item_running != other.dl_plus_item_running)
                return false;
            if (dl_plus_tags != other.dl_plus_tags)
                return false;
        }
        return true;
    }
    bool operator!=(const DL_STATE& other) const {
        return !(*this == other);
    }
};


static bool dls_toggle = false;
static DL_STATE dl_state_prev;

static PADPacketizer *pad_packetizer;


std::vector<std::string> split_string(const std::string &s, const char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string part;

    while (std::getline(ss, part, delimiter))
        result.push_back(part);
    return result;
}










void usage(char* name)
{
    fprintf(stderr, "DAB PAD encoder %s for MOT Slideshow and DLS\n\n"
                    "By CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/) and\n"
                    "Opendigitalradio.org\n\n"
                    "Reads image data from the specified directory, DLS text from a file,\n"
                    "and outputs PAD data to the given FIFO.\n"
                    "  http://opendigitalradio.org\n\n",
#if defined(GITVERSION)
                    GITVERSION
#else
                    PACKAGE_VERSION
#endif
                    );
    fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
    fprintf(stderr, " -d, --dir=DIRNAME      Directory to read images from.\n"
                    " -e, --erase            Erase slides from DIRNAME once they have\n"
                    "                          been encoded.\n"
                    " -s, --sleep=DELAY      Wait DELAY seconds between each slide\n"
                    "                          Default: " STR(SLEEPDELAY_DEFAULT) "\n"
                    " -o, --output=FILENAME  Fifo to write PAD data into.\n"
                    "                          Default: /tmp/pad.fifo\n"
                    " -t, --dls=FILENAME     Fifo or file to read DLS text from.\n"
                    " -p, --pad=LENGTH       Set the pad length.\n"
                    "                          Possible values: %s\n"
                    "                          Default: 58\n"
                    " -c, --charset=ID       ID of the character set encoding used for DLS text input.\n"
                    "                          ID =  0: Complete EBU Latin based repertoire\n"
                    "                          ID =  6: ISO/IEC 10646 using UCS-2 BE\n"
                    "                          ID = 15: ISO/IEC 10646 using UTF-8\n"
                    "                          Default: 15\n"
                    " -r, --remove-dls       Always insert a DLS Remove Label command when replacing a DLS text.\n"
                    " -C, --raw-dls          Do not convert DLS texts to Complete EBU Latin based repertoire\n"
                    "                          character set encoding.\n"
                    " -R, --raw-slides       Do not process slides. Integrity checks and resizing\n"
                    "                          slides is skipped. Use this if you know what you are doing !\n"
                    "                          It is useful only when -d is used\n"
                    " -v, --verbose          Print more information to the console\n",
                    PADPacketizer::ALLOWED_PADLEN.c_str()
           );
}

#define no_argument 0
#define required_argument 1
#define optional_argument 2
int main(int argc, char *argv[])
{
    int ret;
    struct dirent *pDirent;
    size_t padlen = 58;
    bool erase_after_tx = false;
    int  sleepdelay = SLEEPDELAY_DEFAULT;
    bool raw_slides = false;
    int  charset = CHARSET_UTF8;
    bool raw_dls = false;
    bool remove_dls = false;

    const char* sls_dir = NULL;
    const char* output = "/tmp/pad.fifo";
    std::string dls_file;

    const struct option longopts[] = {
        {"charset",    required_argument,  0, 'c'},
        {"raw-dls",    no_argument,        0, 'C'},
        {"remove-dls", no_argument,        0, 'r'},
        {"dir",        required_argument,  0, 'd'},
        {"erase",      no_argument,        0, 'e'},
        {"output",     required_argument,  0, 'o'},
        {"dls",        required_argument,  0, 't'},
        {"pad",        required_argument,  0, 'p'},
        {"sleep",      required_argument,  0, 's'},
        {"raw-slides", no_argument,        0, 'R'},
        {"help",       no_argument,        0, 'h'},
        {"verbose",    no_argument,        0, 'v'},
        {0,0,0,0},
    };

    int ch=0;
    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "eChRrc:d:o:p:s:t:v", longopts, &index);
        switch (ch) {
            case 'c':
                charset = atoi(optarg);
                break;
            case 'C':
                raw_dls = true;
                break;
            case 'r':
                remove_dls = true;
                break;
            case 'd':
                sls_dir = optarg;
                break;
            case 'e':
                erase_after_tx = true;
                break;
            case 'o':
                output = optarg;
                break;
            case 's':
                sleepdelay = atoi(optarg);
                break;
            case 't':
                dls_file = optarg;
                break;
            case 'p':
                padlen = atoi(optarg);
                break;
            case 'R':
                raw_slides = true;
                break;
            case 'v':
                verbose++;
                break;
            case '?':
            case 'h':
                usage(argv[0]);
                return 0;
        }
    }

    if (padlen != PADPacketizer::SHORT_PAD && (padlen < PADPacketizer::VARSIZE_PAD_MIN || padlen > PADPacketizer::VARSIZE_PAD_MAX)) {
        fprintf(stderr, "ODR-PadEnc Error: pad length %zu invalid: Possible values: %s\n",
                padlen, PADPacketizer::ALLOWED_PADLEN.c_str());
        return 2;
    }

    if (sls_dir && not dls_file.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' and DLS from '%s' to '%s'\n",
                sls_dir, dls_file.c_str(), output);
    }
    else if (sls_dir) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' to '%s'. No DLS.\n",
                sls_dir, output);
    }
    else if (not dls_file.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding DLS from '%s' to '%s'. No Slideshow.\n",
                dls_file.c_str(), output);
    }
    else {
        fprintf(stderr, "ODR-PadEnc Error: No DLS nor slideshow to encode !\n");
        usage(argv[0]);
        return 1;
    }

    const char* user_charset;
    switch (charset) {
        case CHARSET_COMPLETE_EBU_LATIN:
            user_charset = "Complete EBU Latin";
            break;
        case CHARSET_EBU_LATIN_CY_GR:
            user_charset = "EBU Latin core, Cyrillic, Greek";
            break;
        case CHARSET_EBU_LATIN_AR_HE_CY_GR:
            user_charset = "EBU Latin core, Arabic, Hebrew, Cyrillic, Greek";
            break;
        case CHARSET_ISO_LATIN_ALPHABET_2:
            user_charset = "ISO Latin Alphabet 2";
            break;
        case CHARSET_UCS2_BE:
            user_charset = "UCS-2 BE";
            break;
        case CHARSET_UTF8:
            user_charset = "UTF-8";
            break;
        default:
            user_charset = "Invalid";
            charset = -1;
            break;
    }

    if (charset == -1) {
        fprintf(stderr, "ODR-PadEnc Error: Invalid charset!\n");
        usage(argv[0]);
        return 1;
    }
    else {
        fprintf(stderr, "ODR-PadEnc using charset %s (%d)\n",
               user_charset, charset);
    }

    if (not raw_dls) {
        switch (charset) {
        case CHARSET_COMPLETE_EBU_LATIN:
            // no conversion needed
            break;
        case CHARSET_UTF8:
            fprintf(stderr, "ODR-PadEnc converting DLS texts to Complete EBU Latin\n");
            break;
        default:
            fprintf(stderr, "ODR-PadEnc Error: DLS conversion to EBU is currently only supported for UTF-8 input!\n");
            return 1;
        }
    }

    int output_fd = open(output, O_WRONLY);
    if (output_fd == -1) {
        perror("ODR-PadEnc Error: failed to open output");
        return 3;
    }

#if HAVE_MAGICKWAND
    MagickWandGenesis();
#endif

    pad_packetizer = new PADPacketizer(padlen);

    std::list<slide_metadata_t> slides_to_transmit;
    History slides_history(MAXHISTORYLEN);

    while(1) {
        if (sls_dir) { // slide + possibly DLS
            DIR *pDir = opendir(sls_dir);
            if (pDir == NULL) {
                fprintf(stderr, "ODR-PadEnc Error: cannot open directory '%s'\n", sls_dir);
                return 1;
            }

            // Add new slides to transmit to list
            while ((pDirent = readdir(pDir)) != NULL) {
                std::string slide = pDirent->d_name;

                // skip dirs beginning with '.'
                if(slide[0] == '.')
                    continue;

                // skip slide params files
                if(slide.length() >= SLS_PARAMS_SUFFIX.length() &&
                        slide.compare(slide.length() - SLS_PARAMS_SUFFIX.length(), SLS_PARAMS_SUFFIX.length(), SLS_PARAMS_SUFFIX) == 0)
                    continue;

                // add slide
                char imagepath[256];
                sprintf(imagepath, "%s/%s", sls_dir, slide.c_str());

                slide_metadata_t md;
                md.filepath = imagepath;
                md.fidx     = slides_history.get_fidx(imagepath);
                slides_to_transmit.push_back(md);

                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc found slide '%s', fidx %d\n", imagepath, md.fidx);
                }
            }

            closedir(pDir);

#ifdef DEBUG
            slides_history.disp_database();
#endif

            // if ATM no slides, transmit at least DLS
            if (slides_to_transmit.empty()) {
                if (not dls_file.empty())
                    writeDLS(output_fd, dls_file, charset, raw_dls, remove_dls);

                sleep(sleepdelay);
            } else {
                // Sort the list in fidx order
                slides_to_transmit.sort();

                // Encode the slides
                for (std::list<slide_metadata_t>::const_iterator it = slides_to_transmit.cbegin();
                        it != slides_to_transmit.cend();
                        ++it) {

                    ret = encodeFile(output_fd, it->filepath, it->fidx, raw_slides);
                    if (ret != 1)
                        fprintf(stderr, "ODR-PadEnc Error: cannot encode file '%s'\n", it->filepath.c_str());

                    if (erase_after_tx) {
                        if (unlink(it->filepath.c_str()) == -1) {
                            fprintf(stderr, "ODR-PadEnc Error: erasing file '%s' failed: ", it->filepath.c_str());
                            perror("");
                        }
                    }

                    // Always retransmit DLS after each slide, we want it to be updated frequently
                    if (not dls_file.empty())
                        writeDLS(output_fd, dls_file, charset, raw_dls, remove_dls);

                    sleep(sleepdelay);
                }

                slides_to_transmit.resize(0);
            }
        } else { // only DLS
            // Always retransmit DLS, we want it to be updated frequently
            writeDLS(output_fd, dls_file, charset, raw_dls, remove_dls);

            sleep(sleepdelay);
        }
    }

    delete pad_packetizer;

    return 1;
}


DATA_GROUP* createDataGroupLengthIndicator(size_t len) {
    DATA_GROUP* dg = new DATA_GROUP(2, 1, 1);    // continuation never used (except for comparison at short X-PAD)
    uint8_vector_t &data = dg->data;

    // Data Group length
    data[0] = (len & 0x3F00) >> 8;
    data[1] = (len & 0x00FF);

    // CRC
    dg->AppendCRC();

    return dg;
}


void warnOnSmallerImage(size_t height, size_t width, const std::string& fname) {
    if (height < 240 || width < 320)
        fprintf(stderr, "ODR-PadEnc Warning: Image '%s' smaller than recommended size (%zu x %zu < 320 x 240 px)\n", fname.c_str(), width, height);
}


/*! Scales the image down if needed,
 * so that it is 320x240 pixels.
 * Automatically reduces the quality to make sure the
 * blobsize is not too large.
 *
 * \return the blobsize
 */
#if HAVE_MAGICKWAND
size_t resizeImage(MagickWand* m_wand, unsigned char** blob, const std::string& fname, bool* jfif_not_png)
{
    unsigned char* blob_png;
    unsigned char* blob_jpg;
    size_t blobsize_png;
    size_t blobsize_jpg;

    size_t height = MagickGetImageHeight(m_wand);
    size_t width  = MagickGetImageWidth(m_wand);

    while (height > 240 || width > 320) {
        if (height/240.0 > width/320.0) {
            width = width * 240.0 / height;
            height = 240;
        }
        else {
            height = height * 320.0 / width;
            width = 320;
        }
        MagickResizeImage(m_wand, width, height, LanczosFilter, 1);
    }

    height = MagickGetImageHeight(m_wand);
    width  = MagickGetImageWidth(m_wand);

    // try PNG (zlib level 9 / possibly adaptive filtering)
    MagickSetImageFormat(m_wand, "png");
    MagickSetImageCompressionQuality(m_wand, 95);
    blob_png = MagickGetImageBlob(m_wand, &blobsize_png);

    // try JPG
    MagickSetImageFormat(m_wand, "jpg");

    blob_jpg = NULL;
    int quality_jpg = 100;
    do {
        free(blob_jpg);
        quality_jpg -= 5;

        MagickSetImageCompressionQuality(m_wand, quality_jpg);
        blob_jpg = MagickGetImageBlob(m_wand, &blobsize_jpg);
    } while (blobsize_jpg > MAXSLIDESIZE && quality_jpg > MINQUALITY);


    // check for max size
    if (blobsize_png > MAXSLIDESIZE && blobsize_jpg > MAXSLIDESIZE) {
        fprintf(stderr, "ODR-PadEnc: Image Size too large after compression: %zu bytes (PNG), %zu bytes (JPEG)\n",
                blobsize_png, blobsize_jpg);
        free(blob_png);
        free(blob_jpg);
        return 0;
    }

    // choose the smaller one (at least one does not exceed the max size)
    *jfif_not_png = blobsize_jpg < blobsize_png;

    if (verbose) {
        if (*jfif_not_png)
            fprintf(stderr, "ODR-PadEnc resized image to %zu x %zu. Size after compression %zu bytes (JPEG, q=%d; PNG was %zu bytes)\n",
                    width, height, blobsize_jpg, quality_jpg, blobsize_png);
        else
            fprintf(stderr, "ODR-PadEnc resized image to %zu x %zu. Size after compression %zu bytes (PNG; JPEG was %zu bytes)\n",
                    width, height, blobsize_png, blobsize_jpg);
    }

    // warn if resized image smaller than default dimension
    warnOnSmallerImage(height, width, fname);

    free(*jfif_not_png ? blob_png : blob_jpg);
    *blob = *jfif_not_png ? blob_jpg : blob_png;
    return *jfif_not_png ? blobsize_jpg : blobsize_png;
}
#endif

int encodeFile(int output_fd, const std::string& fname, int fidx, bool raw_slides)
{
    int ret = 0;
    int nseg, lastseglen, i, last, curseglen;
#if HAVE_MAGICKWAND
    MagickWand *m_wand = NULL;
    MagickBooleanType err;
#endif
    size_t blobsize, height, width;
    bool jpeg_progr;
    unsigned char *blob = NULL;
    unsigned char *curseg = NULL;
    MSCDG msc;
    DATA_GROUP* dgli;
    DATA_GROUP* mscdg;

    size_t orig_quality;
    char*  orig_format = NULL;
    /*! We handle JPEG differently, because we want to avoid recompressing the
     * image if it is suitable as is
     */
    bool orig_is_jpeg = false;

    /*! If the original is a PNG, we transmit it as is, if the resolution is correct
     * and the file is not too large. Otherwise it gets resized and sent as JPEG.
     */
    bool orig_is_png = false;

    /*! By default, we do resize the image to 320x240, with a quality such that
     * the blobsize is at most MAXSLIDESIZE.
     *
     * For JPEG input files that are already at the right resolution and at the
     * right blobsize, we disable this to avoid quality loss due to recompression
     *
     * As device support of this feature is optional, we furthermore require JPEG input
     * files to not have progressive coding.
     */
    bool resize_required = true;

    bool jfif_not_png = true;

    if (!raw_slides) {
#if HAVE_MAGICKWAND

        m_wand = NewMagickWand();

        err = MagickReadImage(m_wand, fname.c_str());
        if (err == MagickFalse) {
            fprintf(stderr, "ODR-PadEnc Error: Unable to load image '%s'\n",
                    fname.c_str());

            goto encodefile_out;
        }

        height       = MagickGetImageHeight(m_wand);
        width        = MagickGetImageWidth(m_wand);
        orig_format  = MagickGetImageFormat(m_wand);
        jpeg_progr   = MagickGetImageInterlaceScheme(m_wand) == JPEGInterlace;

        // By default assume that the image has full quality and can be reduced
        orig_quality = 100;

        // strip unneeded information (profiles, meta data)
        MagickStripImage(m_wand);

        if (orig_format) {
            if (strcmp(orig_format, "JPEG") == 0) {
                orig_quality = MagickGetImageCompressionQuality(m_wand);
                orig_is_jpeg = true;

                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc image: '%s' (id=%d)."
                            " Original size: %zu x %zu. (%s, q=%zu, progr=%s)\n",
                            fname.c_str(), fidx, width, height, orig_format, orig_quality, jpeg_progr ? "y" : "n");
                }
            }
            else if (strcmp(orig_format, "PNG") == 0) {
                orig_is_png = true;
                jfif_not_png = false;

                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc image: '%s' (id=%d)."
                            " Original size: %zu x %zu. (%s)\n",
                            fname.c_str(), fidx, width, height, orig_format);
                }
            }
            else if (verbose) {
                fprintf(stderr, "ODR-PadEnc image: '%s' (id=%d)."
                        " Original size: %zu x %zu. (%s)\n",
                        fname.c_str(), fidx, width, height, orig_format);
            }

            free(orig_format);
        }
        else {
            fprintf(stderr, "ODR-PadEnc Warning: Unable to detect image format of '%s'\n",
                    fname.c_str());

            fprintf(stderr, "ODR-PadEnc image: '%s' (id=%d).  Original size: %zu x %zu.\n",
                    fname.c_str(), fidx, width, height);
        }

        if ((orig_is_jpeg || orig_is_png) && height <= 240 && width <= 320 && not jpeg_progr) {
            // Don't recompress the image and check if the blobsize is suitable
            blob = MagickGetImageBlob(m_wand, &blobsize);

            if (blobsize <= MAXSLIDESIZE) {
                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc image: '%s' (id=%d).  No resize needed: %zu Bytes\n",
                            fname.c_str(), fidx, blobsize);
                }
                resize_required = false;
            }
        }

        if (resize_required) {
            blobsize = resizeImage(m_wand, &blob, fname, &jfif_not_png);
        }
        else {
            // warn if unresized image smaller than default dimension
            warnOnSmallerImage(height, width, fname);
        }

#else
        fprintf(stderr, "ODR-PadEnc has not been compiled with MagickWand, only RAW slides are supported!\n");
        ret = -1;
        goto encodefile_out;
#endif
    }
    else { // Use RAW data, it might not even be a jpg !
        // read file
        FILE* pFile = fopen(fname.c_str(), "rb");
        if (pFile == NULL) {
            fprintf(stderr, "ODR-PadEnc Error: Unable to load file '%s'\n",
                    fname.c_str());
            goto encodefile_out;
        }

        // obtain file size:
        fseek(pFile, 0, SEEK_END);
        blobsize = ftell(pFile);
        rewind(pFile);

        if (verbose) {
            fprintf(stderr, "ODR-PadEnc image: '%s' (id=%d). Raw file: %zu Bytes\n",
                    fname.c_str(), fidx, blobsize);
        }

        if (blobsize > MAXSLIDESIZE) {
            fprintf(stderr, "ODR-PadEnc Warning: blob in raw-slide '%s' too large\n",
                    fname.c_str());
        }

        // allocate memory to contain the whole file:
        blob = (unsigned char*)malloc(sizeof(char) * blobsize);
        if (blob == NULL) {
            fprintf(stderr, "ODR-PadEnc Error: Memory allocation error\n");
            goto encodefile_out;
        }

        // copy the file into the buffer:
        if (fread(blob, blobsize, 1, pFile) != 1) {
            fprintf(stderr, "ODR-PadEnc Error: Could not read file\n");
            goto encodefile_out;
        }

        size_t last_dot = fname.rfind(".");

        // default:
        jfif_not_png = true; // This is how we did it in the past.
                             // It's wrong anyway, so we're at least compatible

        if (last_dot != std::string::npos) {
            std::string file_extension = fname.substr(last_dot, std::string::npos);

            std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);

            if (file_extension == ".png") {
                jfif_not_png = false;
            }
        }

        if (pFile != NULL) {
            fclose(pFile);
        }
    }

    if (blobsize) {
        nseg = blobsize / MAXSEGLEN;
        lastseglen = blobsize % MAXSEGLEN;
        if (lastseglen != 0) {
            nseg++;
        }

        uint8_vector_t mothdr = createMotHeader(blobsize, fidx, jfif_not_png, fname + SLS_PARAMS_SUFFIX);
        // Create the MSC Data Group C-Structure
        createMscDG(&msc, 3, &cindex_header, 0, 1, fidx, &mothdr[0], mothdr.size());
        // Generate the MSC DG frame (Figure 9 en 300 401)
        mscdg = packMscDG(&msc);
        dgli = createDataGroupLengthIndicator(mscdg->data.size());

        pad_packetizer->AddDG(dgli, false);
        pad_packetizer->AddDG(mscdg, false);

        for (i = 0; i < nseg; i++) {
            curseg = blob + i * MAXSEGLEN;
            if (i == nseg-1) {
                curseglen = lastseglen;
                last = 1;
            }
            else {
                curseglen = MAXSEGLEN;
                last = 0;
            }

            createMscDG(&msc, 4, &cindex_body, i, last, fidx, curseg, curseglen);
            mscdg = packMscDG(&msc);
            dgli = createDataGroupLengthIndicator(mscdg->data.size());

            pad_packetizer->AddDG(dgli, false);
            pad_packetizer->AddDG(mscdg, false);
        }

        pad_packetizer->WriteAllPADs(output_fd);

        ret = 1;
    }

encodefile_out:
#if HAVE_MAGICKWAND
    if (m_wand) {
        m_wand = DestroyMagickWand(m_wand);
    }
#endif

    if (blob) {
        free(blob);
    }
    return ret;
}


bool parse_sls_param_id(const std::string &key, const std::string &value, uint8_t &target) {
    int value_int = atoi(value.c_str());
    if (value_int >= 0x00 && value_int <= 0xFF) {
        target = value_int;
        return true;
    }
    fprintf(stderr, "ODR-PadEnc Warning: SLS parameter '%s' %d out of range - ignored\n", key.c_str(), value_int);
    return false;
}


bool check_sls_param_len(const std::string &key, size_t len, size_t len_max) {
    if (len <= len_max)
        return true;
    fprintf(stderr, "ODR-PadEnc Warning: SLS parameter '%s' exceeds its maximum length (%zu > %zu) - ignored\n", key.c_str(), len, len_max);
    return false;
}


void process_mot_params_file(MOTHeader& header, const std::string &params_fname) {
    std::ifstream params_fstream(params_fname);
    if (!params_fstream.is_open())
        return;

    std::string line;
    while (std::getline(params_fstream, line)) {
        // ignore empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // parse key/value pair
        size_t separator_pos = line.find('=');
        if (separator_pos == std::string::npos) {
            fprintf(stderr, "ODR-PadEnc Warning: SLS parameter line '%s' without separator - ignored\n", line.c_str());
            continue;
        }
        std::string key = line.substr(0, separator_pos);
        std::string value = line.substr(separator_pos + 1);
#ifdef DEBUG
        fprintf(stderr, "process_mot_params_file: key: '%s', value: '%s'\n", key.c_str(), value.c_str());
#endif

        if (key == "CategoryID/SlideID") {
            // split value
            std::vector<std::string> params = split_string(value, ' ');
            if (params.size() != 2) {
                fprintf(stderr, "ODR-PadEnc Warning: SLS parameter CategoryID/SlideID value '%s' does not have two parts - ignored\n", value.c_str());
                continue;
            }

            uint8_t id_param[2];
            if (parse_sls_param_id("CategoryID", params[0], id_param[0]) &
                parse_sls_param_id("SlideID", params[1], id_param[1])) {
                header.AddExtension(0x25, id_param, sizeof(id_param));
                if (verbose)
                    fprintf(stderr, "ODR-PadEnc SLS parameter: CategoryID = %d / SlideID = %d\n", id_param[0], id_param[1]);
            }
            continue;
        }
        if (key == "CategoryTitle") {
            if(!check_sls_param_len("CategoryTitle", value.length(), 128))
                continue;

            header.AddExtension(0x26, (uint8_t*) value.c_str(), value.length());
            if (verbose)
                fprintf(stderr, "ODR-PadEnc SLS parameter: CategoryTitle = '%s'\n", value.c_str());
            continue;
        }
        if (key == "ClickThroughURL") {
            if(!check_sls_param_len("ClickThroughURL", value.length(), 512))
                continue;

            header.AddExtension(0x27, (uint8_t*) value.c_str(), value.length());
            if (verbose)
                fprintf(stderr, "ODR-PadEnc SLS parameter: ClickThroughURL = '%s'\n", value.c_str());
            continue;
        }
        if (key == "AlternativeLocationURL") {
            if(!check_sls_param_len("AlternativeLocationURL", value.length(), 512))
                continue;

            header.AddExtension(0x28, (uint8_t*) value.c_str(), value.length());
            if (verbose)
                fprintf(stderr, "ODR-PadEnc SLS parameter: AlternativeLocationURL = '%s'\n", value.c_str());
            continue;
        }

        fprintf(stderr, "ODR-PadEnc Warning: SLS parameter '%s' unknown - ignored\n", key.c_str());
    }
}


uint8_vector_t createMotHeader(size_t blobsize, int fidx, bool jfif_not_png, const std::string &params_fname)
{
    // prepare ContentName
    uint8_t cntemp[10];     // = 1 + 8 + 1 = charset + name + terminator
    cntemp[0] = 0x0 << 4;   // charset: 0 (Complete EBU Latin based) - doesn't really matter here
    snprintf((char*) (cntemp + 1), sizeof(cntemp) - 1, "%04d.%s", fidx, jfif_not_png ? "jpg" : "png");

    // MOT header - content type: image, content subtype: JFIF / PNG
    MOTHeader header(blobsize, 0x02, jfif_not_png ? 0x001 : 0x003);

    // TriggerTime: NOW
    uint8_t triggertime_now[4] = {0x00};
    header.AddExtension(0x05, triggertime_now, sizeof(triggertime_now));

    // ContentName: XXXX.jpg / XXXX.png
    header.AddExtension(0x0C, cntemp, sizeof(cntemp) - 1);   // omit terminator

    // process params file if present
    process_mot_params_file(header, params_fname);

    if (verbose)
        fprintf(stderr, "ODR-PadEnc writing image as '%s'\n", cntemp + 1);

    return header.GetData();
}


void createMscDG(MSCDG* msc, unsigned short int dgtype,
        int *cindex, unsigned short int segnum, unsigned short int lastseg,
        unsigned short int tid, unsigned char* data,
        unsigned short int datalen)
{
    msc->extflag = 0;
    msc->crcflag = 1;
    msc->segflag = 1;
    msc->accflag = 1;
    msc->dgtype = dgtype;
    msc->cindex = *cindex;
    msc->rindex = 0;
    msc->last = lastseg;
    msc->segnum = segnum;
    msc->rfa = 0;
    msc->tidflag = 1;
    msc->lenid = 2;
    msc->tid = tid;
    msc->segdata = data;
    msc->rcount = 0;
    msc->seglen = datalen;

    *cindex = (*cindex + 1) % 16;   // increment continuity index
}


DATA_GROUP* packMscDG(MSCDG* msc)
{
    DATA_GROUP* dg = new DATA_GROUP(9 + msc->seglen, 12, 13);
    uint8_vector_t &b = dg->data;

    // headers
    b[0] = (msc->extflag<<7) | (msc->crcflag<<6) | (msc->segflag<<5) |
           (msc->accflag<<4) | msc->dgtype;

    b[1] = (msc->cindex<<4) | msc->rindex;
    b[2] = (msc->last<<7) | ((msc->segnum & 0x7F00) >> 8);
    b[3] =  msc->segnum & 0x00FF;
    b[4] = 0;
    b[4] = (msc->rfa << 5) | (msc->tidflag << 4) | msc->lenid;
    b[5] = (msc->tid & 0xFF00) >> 8;
    b[6] =  msc->tid & 0x00FF;
    b[7] = (msc->rcount << 5) | ((msc->seglen & 0x1F00)>>8);
    b[8] =  msc->seglen & 0x00FF;

    // data field
    memcpy(&b[9], msc->segdata, msc->seglen);

    // CRC
    dg->AppendCRC();

    return dg;
}


DATA_GROUP* createDynamicLabelCommand(uint8_t command) {
    DATA_GROUP* dg = new DATA_GROUP(2, 2, 3);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg + last seg + command flag + command
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (1 << 6) +
            (1 << 5) +
            (1 << 4) +
            command;

    // prefix: charset (though irrelevant here)
    seg_data[1] = CHARSET_COMPLETE_EBU_LATIN;

    // CRC
    dg->AppendCRC();

    return dg;
}

DATA_GROUP* createDynamicLabelPlus(const DL_STATE& dl_state) {
    size_t tags_size = dl_state.dl_plus_tags.size();
    size_t len_dl_plus_cmd_field = 1 + 3 * tags_size;
    DATA_GROUP* dg = new DATA_GROUP(2 + len_dl_plus_cmd_field, 2, 3);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg + last seg + command flag + command
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (1 << 6) +
            (1 << 5) +
            (1 << 4) +
            DLS_CMD_DL_PLUS;

    // prefix: link bit + length
    seg_data[1] =
            (dls_toggle ? (1 << 7) : 0) +
            (len_dl_plus_cmd_field - 1);    // -1 !

    // DL Plus tags command: CId + IT + IR + NT
    seg_data[2] =
            (DL_PLUS_CMD_TAGS << 4) +
            (dl_state.dl_plus_item_toggle ? (1 << 3) : 0) +
            (dl_state.dl_plus_item_running ? (1 << 2) : 0) +
            (tags_size - 1);                // -1 !

    for (size_t i = 0; i < tags_size; i++) {
        // DL Plus tags command: Content Type + Start Marker + Length Marker
        seg_data[3 + 3 * i] = dl_state.dl_plus_tags[i].content_type & 0x7F;
        seg_data[4 + 3 * i] = dl_state.dl_plus_tags[i].start_marker & 0x7F;
        seg_data[5 + 3 * i] = dl_state.dl_plus_tags[i].length_marker & 0x7F;
    }

    // CRC
    dg->AppendCRC();

    return dg;
}


bool parse_dl_param_bool(const std::string &key, const std::string &value, bool &target) {
    if (value == "0") {
        target = 0;
        return true;
    }
    if (value == "1") {
        target = 1;
        return true;
    }
    fprintf(stderr, "ODR-PadEnc Warning: DL parameter '%s' has unsupported value '%s' - ignored\n", key.c_str(), value.c_str());
    return false;
}

bool parse_dl_param_int_dl_plus_tag(const std::string &key, const std::string &value, int &target) {
    int value_int = atoi(value.c_str());
    if (value_int >= 0x00 && value_int <= 0x7F) {
        target = value_int;
        return true;
    }
    fprintf(stderr, "ODR-PadEnc Warning: DL Plus tag parameter '%s' %d out of range - ignored\n", key.c_str(), value_int);
    return false;
}

void parse_dl_params(std::ifstream &dls_fstream, DL_STATE &dl_state) {
    std::string line;
    while (std::getline(dls_fstream, line)) {
        // return on params close
        if (line == DL_PARAMS_CLOSE)
            return;

        // ignore empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // parse key/value pair
        size_t separator_pos = line.find('=');
        if (separator_pos == std::string::npos) {
            fprintf(stderr, "ODR-PadEnc Warning: DL parameter line '%s' without separator - ignored\n", line.c_str());
            continue;
        }
        std::string key = line.substr(0, separator_pos);
        std::string value = line.substr(separator_pos + 1);
#ifdef DEBUG
        fprintf(stderr, "parse_dl_params: key: '%s', value: '%s'\n", key.c_str(), value.c_str());
#endif

        if (key == "DL_PLUS") {
            parse_dl_param_bool(key, value, dl_state.dl_plus_enabled);
            continue;
        }
        if (key == "DL_PLUS_ITEM_TOGGLE") {
            parse_dl_param_bool(key, value, dl_state.dl_plus_item_toggle);
            continue;
        }
        if (key == "DL_PLUS_ITEM_RUNNING") {
            parse_dl_param_bool(key, value, dl_state.dl_plus_item_running);
            continue;
        }
        if (key == "DL_PLUS_TAG") {
            if (dl_state.dl_plus_tags.size() == 4) {
                fprintf(stderr, "ODR-PadEnc Warning: DL Plus tag ignored, as already four tags present\n");
                continue;
            }

            // split value
            std::vector<std::string> params = split_string(value, ' ');
            if (params.size() != 3) {
                fprintf(stderr, "ODR-PadEnc Warning: DL Plus tag value '%s' does not have three parts - ignored\n", value.c_str());
                continue;
            }

            int content_type, start_marker, length_marker;
            if (parse_dl_param_int_dl_plus_tag("content_type", params[0], content_type) &
                parse_dl_param_int_dl_plus_tag("start_marker", params[1], start_marker) &
                parse_dl_param_int_dl_plus_tag("length_marker", params[2], length_marker))
                dl_state.dl_plus_tags.push_back(DL_PLUS_TAG(content_type, start_marker, length_marker));
            continue;
        }

        fprintf(stderr, "ODR-PadEnc Warning: DL parameter '%s' unknown - ignored\n", key.c_str());
    }

    fprintf(stderr, "ODR-PadEnc Warning: no param closing tag, so the DLS text will be empty\n");
}


void writeDLS(int output_fd, const std::string& dls_file, uint8_t charset, bool raw_dls, bool remove_dls)
{
    std::ifstream dls_fstream(dls_file);
    if (!dls_fstream.is_open()) {
        std::cerr << "Could not open " << dls_file << std::endl;
        return;
    }

    DL_STATE dl_state;

    std::vector<std::string> dls_lines;

    std::string line;
    // Read and convert lines one by one because the converter doesn't understand
    // line endings
    while (std::getline(dls_fstream, line)) {
        if (line.empty())
            continue;
        if (line == DL_PARAMS_OPEN) {
            parse_dl_params(dls_fstream, dl_state);
        } else {
            if (not raw_dls && charset == CHARSET_UTF8) {
                dls_lines.push_back(charset_converter.convert(line));
            }
            else {
                dls_lines.push_back(line);
            }
            // TODO handle the other charsets accordingly
        }
    }

    std::stringstream ss;
    for (size_t i = 0; i < dls_lines.size(); i++) {
        if (i != 0) {
            if (charset == CHARSET_UCS2_BE)
                ss << '\0' << '\n';
            else
                ss << '\n';
        }

        // UCS-2 BE: if from file the first byte of \0\n remains, remove it
        if (charset == CHARSET_UCS2_BE && dls_lines[i].size() % 2) {
            dls_lines[i].resize(dls_lines[i].size() - 1);
        }

        ss << dls_lines[i];
    }

    dl_state.dl_text = ss.str();
    if (dl_state.dl_text.size() > MAXDLS)
        dl_state.dl_text.resize(MAXDLS);


    // if DL Plus enabled, but no DL Plus tags were added, add the required DUMMY tag
    if (dl_state.dl_plus_enabled && dl_state.dl_plus_tags.empty())
        dl_state.dl_plus_tags.push_back(DL_PLUS_TAG());

    if (not raw_dls)
        charset = CHARSET_COMPLETE_EBU_LATIN;


    // toggle the toggle bit only on new DL state
    bool dl_state_is_new = dl_state != dl_state_prev;
    if (verbose) {
        fprintf(stderr, "ODR-PadEnc writing %s DLS text \"%s\"\n", dl_state_is_new ? "new" : "old", dl_state.dl_text.c_str());
        if (dl_state.dl_plus_enabled) {
            fprintf(
                    stderr, "ODR-PadEnc writing %s DL Plus tags (IT/IR: %d/%d): ",
                    dl_state_is_new ? "new" : "old",
                    dl_state.dl_plus_item_toggle ? 1 : 0,
                    dl_state.dl_plus_item_running ? 1 : 0);
            for (dl_plus_tags_t::const_iterator it = dl_state.dl_plus_tags.begin(); it != dl_state.dl_plus_tags.end(); it++) {
                if (it != dl_state.dl_plus_tags.begin())
                    fprintf(stderr, ", ");
                fprintf(stderr, "%d (S/L: %d/%d)", it->content_type, it->start_marker, it->length_marker);
            }
            fprintf(stderr, "\n");
        }
    }

    DATA_GROUP *remove_label_dg = NULL;
    if (dl_state_is_new) {
        if (remove_dls)
            remove_label_dg = createDynamicLabelCommand(DLS_CMD_REMOVE_LABEL);

        dls_toggle = !dls_toggle;   // indicate changed text

        dl_state_prev = dl_state;
    }

    prepend_dl_dgs(dl_state, charset);
    if (remove_label_dg)
        pad_packetizer->AddDG(remove_label_dg, true);
    pad_packetizer->WriteAllPADs(output_fd);
}


int dls_count(const std::string& text) {
    size_t text_len = text.size();
    return text_len / DLS_SEG_LEN_CHAR_MAX + (text_len % DLS_SEG_LEN_CHAR_MAX ? 1 : 0);
}


DATA_GROUP* dls_get(const std::string& text, uint8_t charset, int seg_index) {
    bool first_seg = seg_index == 0;
    bool last_seg  = seg_index == dls_count(text) - 1;

    int seg_text_offset = seg_index * DLS_SEG_LEN_CHAR_MAX;
    const char *seg_text_start = text.c_str() + seg_text_offset;
    size_t seg_text_len = std::min(text.size() - seg_text_offset, DLS_SEG_LEN_CHAR_MAX);

    DATA_GROUP* dg = new DATA_GROUP(DLS_SEG_LEN_PREFIX + seg_text_len, 2, 3);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg? + last seg? + (seg len - 1)
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (first_seg  ? (1 << 6) : 0) +
            (last_seg   ? (1 << 5) : 0) +
            (seg_text_len - 1);

    // prefix: charset / seg index
    seg_data[1] = (first_seg ? charset : seg_index) << 4;

    // character field
    memcpy(&seg_data[DLS_SEG_LEN_PREFIX], seg_text_start, seg_text_len);

    // CRC
    dg->AppendCRC();

#ifdef DEBUG
    fprintf(stderr, "DL segment:");
    for (int i = 0; i < seg_data.size(); i++)
        fprintf(stderr, " %02x", seg_data[i]);
    fprintf(stderr, "\n");
#endif
    return dg;
}


void prepend_dl_dgs(const DL_STATE& dl_state, uint8_t charset) {
    // process all DL segments
    int seg_count = dls_count(dl_state.dl_text);
    std::vector<DATA_GROUP*> segs;
    for (int seg_index = 0; seg_index < seg_count; seg_index++) {
#ifdef DEBUG
        fprintf(stderr, "Segment number %d\n", seg_index + 1);
#endif
        segs.push_back(dls_get(dl_state.dl_text, charset, seg_index));
    }

    // if enabled, add DL Plus data group
    if (dl_state.dl_plus_enabled)
        segs.push_back(createDynamicLabelPlus(dl_state));

    // prepend to packetizer
    pad_packetizer->AddDGs(segs, true);

#ifdef DEBUG
    fprintf(stderr, "PAD length: %d\n", padlen);
    fprintf(stderr, "DLS text: %s\n", text.c_str());
    fprintf(stderr, "Number of DL segments: %d\n", seg_count);
#endif
}

int History::find(const fingerprint_t& fp) const
{
    size_t i;
    for (i = 0; i < m_database.size(); i++) {
        if (m_database[i] == fp) {
            // return the id of fingerprint found
            return m_database[i].fidx;
        }
    }

    // return -1 when the database doesn't contain this fingerprint
    return -1;
}

void History::add(fingerprint_t& fp)
{
    m_database.push_back(fp);

    if (m_database.size() > m_hist_size) {
        m_database.pop_front();
    }
}

void History::disp_database()
{
    size_t id;
    printf("HISTORY DATABASE:\n");
    if (m_database.empty()) {
        printf(" empty\n");
    }
    else {
        for (id = 0; id < m_database.size(); id++) {
            printf(" id %4zu: ", id);
            m_database[id].disp();
        }
    }
    printf("-----------------\n");
}

int History::get_fidx(const char* filepath)
{
    fingerprint_t fp;

    fp.load_from_file(filepath);

    int idx = find(fp);

    if (idx < 0) {
        idx = m_last_given_fidx++;
        fp.fidx = idx;

        if (m_last_given_fidx > MAXSLIDEID) {
            m_last_given_fidx = 0;
        }

        add(fp);
    }

    return idx;
}
