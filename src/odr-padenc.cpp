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

#include "odr-padenc.h"


static PadEncoder *pad_encoder = NULL;

static void break_handler(int) {
    fprintf(stderr, "...ODR-PadEnc exits...\n");
    if(pad_encoder)
        pad_encoder->DoExit();
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
    PadEncoderOptions options_default;
    fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
    fprintf(stderr, " -d, --dir=DIRNAME      Directory to read images from.\n"
                    " -e, --erase            Erase slides from DIRNAME once they have\n"
                    "                          been encoded.\n"
                    " -s, --sleep=DELAY      Wait DELAY seconds between each slide\n"
                    "                          Default: %d\n"
                    " -o, --output=FILENAME  FIFO to write PAD data into.\n"
                    "                          Default: %s\n"
                    " -t, --dls=FILENAME     FIFO or file to read DLS text from.\n"
                    "                          If specified more than once, use next file after DELAY seconds.\n"
                    " -p, --pad=LENGTH       Set the PAD length.\n"
                    "                          Possible values: %s\n"
                    "                          Default: %zu\n"
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
                    options_default.sleepdelay,
                    options_default.output,
                    PADPacketizer::ALLOWED_PADLEN.c_str(),
                    options_default.padlen
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
    header();

    // get/check options
    PadEncoderOptions options;

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
                options.dl_params.charset = (DABCharset) atoi(optarg);
                break;
            case 'C':
                options.dl_params.raw_dls = true;
                break;
            case 'r':
                options.dl_params.remove_dls = true;
                break;
            case 'd':
                options.sls_dir = optarg;
                break;
            case 'e':
                options.erase_after_tx = true;
                break;
            case 'o':
                options.output = optarg;
                break;
            case 's':
                options.sleepdelay = atoi(optarg);
                break;
            case 't':   // can be used more than once!
                options.dls_files.push_back(optarg);
                break;
            case 'p':
                options.padlen = atoi(optarg);
                break;
            case 'R':
                options.raw_slides = true;
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

    if (!PADPacketizer::CheckPADLen(options.padlen)) {
        fprintf(stderr, "ODR-PadEnc Error: PAD length %zu invalid: Possible values: %s\n",
                options.padlen, PADPacketizer::ALLOWED_PADLEN.c_str());
        return 2;
    }

    if (options.sls_dir && not options.dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' and DLS from %s to '%s' (PAD length: %zu)\n",
                options.sls_dir, list_dls_files(options.dls_files).c_str(), options.output, options.padlen);
    }
    else if (options.sls_dir) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' to '%s' (PAD length: %zu). No DLS.\n",
                options.sls_dir, options.output, options.padlen);
    }
    else if (not options.dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding DLS from %s to '%s' (PAD length: %zu). No Slideshow.\n",
                list_dls_files(options.dls_files).c_str(), options.output, options.padlen);
    }
    else {
        fprintf(stderr, "ODR-PadEnc Error: Neither DLS nor Slideshow to encode !\n");
        usage(argv[0]);
        return 1;
    }

    const char* user_charset;
    switch (options.dl_params.charset) {
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
           user_charset, (int) options.dl_params.charset);

    if (not options.dl_params.raw_dls) {
        switch (options.dl_params.charset) {
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

    // invoke encoder
    pad_encoder = new BurstPadEncoder(options);
    int result = pad_encoder->Main();
    delete pad_encoder;

    return result;
}


// --- PadEncoder -----------------------------------------------------------------
void PadEncoder::DoExit() {
    std::lock_guard<std::mutex> lock(status_mutex);

    do_exit = true;
}

int PadEncoder::Main() {
    output_fd = open(options.output, O_WRONLY);
    if (output_fd == -1) {
        perror("ODR-PadEnc Error: failed to open output");
        return 3;
    }

#if HAVE_MAGICKWAND
    MagickWandGenesis();
    if (verbose)
        fprintf(stderr, "ODR-PadEnc using ImageMagick version '%s'\n", GetMagickVersion(NULL));
#endif

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

    // invoke actual encoder
    int result = Encode();

    // cleanup
    if(close(output_fd)) {
        perror("ODR-PadEnc Error: failed to close output");
        return 1;
    }

#if HAVE_MAGICKWAND
    MagickWandTerminus();
#endif

    return result;
}


// --- BurstPadEncoder -----------------------------------------------------------------
const int BurstPadEncoder::DLS_REPETITION_WHILE_SLS = 50; // PADs

int BurstPadEncoder::Encode() {
    PADPacketizer pad_packetizer(options.padlen);
    DLSManager dls_manager(&pad_packetizer);
    SLSManager sls_manager(&pad_packetizer);
    SlideStore slides;

    std::chrono::steady_clock::time_point next_run = std::chrono::steady_clock::now();
    int curr_dls_file = 0;

    while(!do_exit) {
        // try to read slides dir (if present)
        if (options.sls_dir && slides.Empty()) {
            if (!slides.InitFromDir(options.sls_dir))
                return 1;
        }

        // if slides available, encode the first one
        if (!slides.Empty()) {
            slide_metadata_t slide = slides.GetSlide();

            if (!sls_manager.encodeFile(slide.filepath, slide.fidx, options.raw_slides))
                fprintf(stderr, "ODR-PadEnc Error: cannot encode file '%s'\n", slide.filepath.c_str());

            if (options.erase_after_tx) {
                if (unlink(slide.filepath.c_str()) == -1)
                    perror(("ODR-PadEnc Error: erasing file '" + slide.filepath +"' failed").c_str());
            }

            // while flushing, insert DLS (if present) after a certain PAD amout
            while (pad_packetizer.QueueFilled()) {
                if (not options.dls_files.empty())
                    dls_manager.writeDLS(options.dls_files[curr_dls_file], options.dl_params);

                pad_packetizer.WriteAllPADs(output_fd, DLS_REPETITION_WHILE_SLS);
            }
        }

        // encode (a last) DLS (if present)
        if (not options.dls_files.empty()) {
            dls_manager.writeDLS(options.dls_files[curr_dls_file], options.dl_params);

            // switch to next DLS file
            curr_dls_file = (curr_dls_file + 1) % options.dls_files.size();
        }

        // flush all remaining PADs
        pad_packetizer.WriteAllPADs(output_fd);

        // sleep until next run
        next_run += std::chrono::seconds(options.sleepdelay);
        std::this_thread::sleep_until(next_run);
    }

    return 0;
}
