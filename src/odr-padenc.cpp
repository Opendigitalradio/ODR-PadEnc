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
#include <list>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <getopt.h>

#include "pad_common.h"
#include "dls.h"
#include "sls.h"


static const int    SLEEPDELAY_DEFAULT = 10; // seconds



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
                    "                          Default: %d\n"
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
                    SLEEPDELAY_DEFAULT, PADPacketizer::ALLOWED_PADLEN.c_str()
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
    DABCharset charset = DABCharset::UTF8;
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
                charset = (DABCharset) atoi(optarg);
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
           user_charset, charset);

    if (not raw_dls) {
        switch (charset) {
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
#endif

    PADPacketizer pad_packetizer(padlen);
    DLSManager dls_manager(&pad_packetizer);
    SLSManager sls_manager;

    std::list<slide_metadata_t> slides_to_transmit;
    History slides_history(History::MAXHISTORYLEN);

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
                if(slide.length() >= SLSManager::SLS_PARAMS_SUFFIX.length() &&
                        slide.compare(slide.length() - SLSManager::SLS_PARAMS_SUFFIX.length(), SLSManager::SLS_PARAMS_SUFFIX.length(), SLSManager::SLS_PARAMS_SUFFIX) == 0)
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
                if (not dls_file.empty()) {
                    dls_manager.writeDLS(dls_file, charset, raw_dls, remove_dls);
                    pad_packetizer.WriteAllPADs(output_fd);
                }

                sleep(sleepdelay);
            } else {
                // Sort the list in fidx order
                slides_to_transmit.sort();

                // Encode the slides
                for (std::list<slide_metadata_t>::const_iterator it = slides_to_transmit.cbegin();
                        it != slides_to_transmit.cend();
                        ++it) {

                    ret = sls_manager.encodeFile(pad_packetizer, it->filepath, it->fidx, raw_slides);
                    if (ret != 1)
                        fprintf(stderr, "ODR-PadEnc Error: cannot encode file '%s'\n", it->filepath.c_str());

                    if (erase_after_tx) {
                        if (unlink(it->filepath.c_str()) == -1) {
                            fprintf(stderr, "ODR-PadEnc Error: erasing file '%s' failed: ", it->filepath.c_str());
                            perror("");
                        }
                    }

                    // while flushing, insert DLS after a certain PAD amout
                    while (pad_packetizer.QueueFilled()) {
                        if (not dls_file.empty())
                            dls_manager.writeDLS(dls_file, charset, raw_dls, remove_dls);

                        pad_packetizer.WriteAllPADs(output_fd, DLSManager::DLS_REPETITION_WHILE_SLS);
                    }

                    // after the slide, output a last DLS
                    if (not dls_file.empty())
                        dls_manager.writeDLS(dls_file, charset, raw_dls, remove_dls);
                    pad_packetizer.WriteAllPADs(output_fd);

                    sleep(sleepdelay);
                }

                slides_to_transmit.resize(0);
            }
        } else { // only DLS
            // Always retransmit DLS, we want it to be updated frequently
            dls_manager.writeDLS(dls_file, charset, raw_dls, remove_dls);
            pad_packetizer.WriteAllPADs(output_fd);

            sleep(sleepdelay);
        }
    }

    return 1;
}
