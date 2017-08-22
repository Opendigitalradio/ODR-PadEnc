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
    \brief Generate PAD data for MOT Slideshow and DLS

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "common.h"

#include <stdlib.h>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <fcntl.h>
#include <getopt.h>

#include "pad_common.h"
#include "dls.h"
#include "sls.h"


static const int SLEEPDELAY_DEFAULT = 10; // seconds
static const int DLS_REPETITION_WHILE_SLS = 50;

static bool do_exit = false;


static void break_handler(int) {
    fprintf(stderr, "...ODR-PadEnc exits...\n");
    do_exit = true;
}


static void header() {
    fprintf(stderr, "ODR-PadEnc %s - DAB PAD encoder for MOT Slideshow and DLS\n\n"
                    "By CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/) and\n"
                    "Opendigitalradio.org\n\n"
                    "Reads image data from the specified directory, DLS text from a file,\n"
                    "and outputs PAD data to the given FIFO.\n"
                    "  https://opendigitalradio.org\n\n",
#if defined(GITVERSION)
                    GITVERSION
#else
                    PACKAGE_VERSION
#endif
                    );
}

static void usage(const char* name) {
    fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
    fprintf(stderr, " -d, --dir=DIRNAME      Directory to read images from.\n"
                    " -e, --erase            Erase slides from DIRNAME once they have\n"
                    "                          been encoded.\n"
                    " -s, --sleep=DELAY      Wait DELAY seconds between each slide\n"
                    "                          Default: %d\n"
                    " -o, --output=FILENAME  FIFO to write PAD data into.\n"
                    "                          Default: /tmp/pad.fifo\n"
                    " -t, --dls=FILENAME     FIFO or file to read DLS text from.\n"
                    "                          If specified more than once, use next file after DELAY seconds.\n"
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
                    SLEEPDELAY_DEFAULT, PADPacketizer::ALLOWED_PADLEN.c_str()
           );
}


static std::string list_dls_files(std::vector<std::string> dls_files) {
    std::string result = "";
    for(std::string dls_file : dls_files) {
        if(!result.empty())
            result += "/";
        result += "'" + dls_file + "'";
    }
    return result;
}


int main(int argc, char *argv[]) {
    size_t padlen = 58;
    bool erase_after_tx = false;
    int  sleepdelay = SLEEPDELAY_DEFAULT;
    bool raw_slides = false;
    DL_PARAMS dl_params;

    const char* sls_dir = NULL;
    const char* output = "/tmp/pad.fifo";
    std::vector<std::string> dls_files;
    int curr_dls_file = 0;

    header();

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

    int ch;
    while((ch = getopt_long(argc, argv, "eChRrc:d:o:p:s:t:v", longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':
                dl_params.charset = (DABCharset) atoi(optarg);
                break;
            case 'C':
                dl_params.raw_dls = true;
                break;
            case 'r':
                dl_params.remove_dls = true;
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
            case 't':   // can be used more than once!
                dls_files.push_back(optarg);
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

    if (!PADPacketizer::CheckPADLen(padlen)) {
        fprintf(stderr, "ODR-PadEnc Error: PAD length %zu invalid: Possible values: %s\n",
                padlen, PADPacketizer::ALLOWED_PADLEN.c_str());
        return 2;
    }

    if (sls_dir && not dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' and DLS from %s to '%s'\n",
                sls_dir, list_dls_files(dls_files).c_str(), output);
    }
    else if (sls_dir) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' to '%s'. No DLS.\n",
                sls_dir, output);
    }
    else if (not dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding DLS from %s to '%s'. No Slideshow.\n",
                list_dls_files(dls_files).c_str(), output);
    }
    else {
        fprintf(stderr, "ODR-PadEnc Error: Neither DLS nor Slideshow to encode !\n");
        usage(argv[0]);
        return 1;
    }

    const char* user_charset;
    switch (dl_params.charset) {
        case DABCharset::COMPLETE_EBU_LATIN:
            user_charset = "Complete EBU Latin";
            break;
        case DABCharset::EBU_LATIN_CY_GR:
            user_charset = "EBU Latin core, Cyrillic, Greek";
            break;
        case DABCharset::EBU_LATIN_AR_HE_CY_GR:
            user_charset = "EBU Latin core, Arabic, Hebrew, Cyrillic, Greek";
            break;
        case DABCharset::ISO_LATIN_ALPHABET_2:
            user_charset = "ISO Latin Alphabet 2";
            break;
        case DABCharset::UCS2_BE:
            user_charset = "UCS-2 BE";
            break;
        case DABCharset::UTF8:
            user_charset = "UTF-8";
            break;
        default:
            fprintf(stderr, "ODR-PadEnc Error: Invalid charset!\n");
            usage(argv[0]);
            return 1;
    }

    fprintf(stderr, "ODR-PadEnc using charset %s (%d)\n",
           user_charset, (int) dl_params.charset);

    if (not dl_params.raw_dls) {
        switch (dl_params.charset) {
        case DABCharset::COMPLETE_EBU_LATIN:
            // no conversion needed
            break;
        case DABCharset::UTF8:
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
    if (verbose)
        fprintf(stderr, "ODR-PadEnc using ImageMagick version '%s'\n", GetMagickVersion(NULL));
#endif

    PADPacketizer pad_packetizer(padlen);
    DLSManager dls_manager(&pad_packetizer);
    SLSManager sls_manager(&pad_packetizer);
    SlideStore slides;

    std::chrono::steady_clock::time_point next_run = std::chrono::steady_clock::now();

    // handle signals
    if(signal(SIGINT, break_handler) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGINT handler");
        return 1;
    }
    if(signal(SIGTERM, break_handler) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGTERM handler");
        return 1;
    }
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGPIPE to be ignored");
        return 1;
    }

    while(!do_exit) {
        // try to read slides dir (if present)
        if (sls_dir && slides.Empty()) {
            if (!slides.InitFromDir(sls_dir))
                return 1;
        }

        // if slides available, encode the first one
        if (!slides.Empty()) {
            slide_metadata_t slide = slides.GetSlide();

            if (!sls_manager.encodeFile(slide.filepath, slide.fidx, raw_slides))
                fprintf(stderr, "ODR-PadEnc Error: cannot encode file '%s'\n", slide.filepath.c_str());

            if (erase_after_tx) {
                if (unlink(slide.filepath.c_str()) == -1) {
                    fprintf(stderr, "ODR-PadEnc Error: erasing file '%s' failed: ", slide.filepath.c_str());
                    perror("");
                }
            }

            // while flushing, insert DLS (if present) after a certain PAD amout
            while (pad_packetizer.QueueFilled()) {
                if (not dls_files.empty())
                    dls_manager.writeDLS(dls_files[curr_dls_file], dl_params);

                pad_packetizer.WriteAllPADs(output_fd, DLS_REPETITION_WHILE_SLS);
            }
        }

        // encode (a last) DLS (if present)
        if (not dls_files.empty()) {
            dls_manager.writeDLS(dls_files[curr_dls_file], dl_params);

            // switch to next DLS file
            curr_dls_file = (curr_dls_file + 1) % dls_files.size();
        }

        // flush all remaining PADs
        pad_packetizer.WriteAllPADs(output_fd);

        // sleep until next run
        next_run += std::chrono::seconds(sleepdelay);
        std::this_thread::sleep_until(next_run);
    }


    // cleanup
    if(close(output_fd)) {
        perror("ODR-PadEnc Error: failed to close output");
        return 1;
    }

#if HAVE_MAGICKWAND
    MagickWandTerminus();
#endif

    return 0;
}
