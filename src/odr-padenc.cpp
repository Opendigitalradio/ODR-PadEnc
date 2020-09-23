/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014-2020 Matthias P. Braendli (http://opendigitalradio.org)

    Copyright (C) 2015-2019 Stefan Pöschel (http://opendigitalradio.org)

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
#include <memory>

std::atomic<bool> do_exit;

static void break_handler(int) {
    fprintf(stderr, "...ODR-PadEnc exits...\n");
    do_exit.store(true);
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
    fprintf(stderr, " -d, --dir=DIRNAME         Directory to read images from.\n"
                    " -e, --erase               Erase slides from DIRNAME once they have\n"
                    "                             been encoded.\n"
                    " -s, --sleep=DUR           Wait DUR seconds between each slide\n"
                    "                             Default: %d\n"
                    " -o, --output=IDENTIFIER   Socket to communicate with audio encoder\n"
                    " --dump-current-slide=F1   Write the slide currently being transmitted to the file F1\n"
                    " --dump-completed-slide=F2 Once the slide is transmitted, move the file from F1 to F2\n"
                    " -t, --dls=FILENAME        FIFO or file to read DLS text from.\n"
                    "                             If specified more than once, use next file after -l delay.\n"
                    " -c, --charset=ID          ID of the character set encoding used for DLS text input.\n"
                    "                             ID =  0: Complete EBU Latin based repertoire\n"
                    "                             ID =  6: ISO/IEC 10646 using UCS-2 BE\n"
                    "                             ID = 15: ISO/IEC 10646 using UTF-8\n"
                    "                             Default: 15\n"
                    " -r, --remove-dls          Always insert a DLS Remove Label command when replacing a DLS text.\n"
                    " -C, --raw-dls             Do not convert DLS texts to Complete EBU Latin based repertoire\n"
                    "                             character set encoding.\n"
                    " -I, --item-state=FILENAME FIFO or file to read the DL Plus Item Toggle/Running bits from (instead of the current DLS file).\n"
                    " -m, --max-slide-size=SIZE Recompress slide if above the specified maximum size in bytes.\n"
                    "                             Default: %zu (Simple Profile)\n"
                    " -R, --raw-slides          Do not process slides. Integrity checks and resizing\n"
                    "                             slides is skipped. Use this if you know what you are doing !\n"
                    "                             Slides whose name ends in _PadEncRawMode.jpg or _PadEncRawMode.png are always transmitted unprocessed, regardless of\n"
                    "                             the -R option being set \n"
                    "                             It is useful only when -d is used\n"
                    " -v, --verbose             Print more information to the console (may be used more than once)\n"
                    " --version                 Print version information and quit\n"
                    " -l, --label=DUR           Wait DUR seconds between each label (if more than one file used)\n"
                    "                             Default: %d\n"
                    " -L, --label-ins=DUR       Insert label every DUR milliseconds\n"
                    "                             Default: %d\n"
                    " -X, --xpad-interval=COUNT Output X-PAD every COUNT frames/AUs (otherwise: only F-PAD)\n"
                    "                             Default: %d\n"
                    "\n"
                    "The PAD length is configured on the audio encoder and communicated over the socket to ODR-PadEnc\n"
                    "Allowed PAD lengths are: %s\n",
                    options_default.slide_interval,
                    options_default.max_slide_size,
                    options_default.label_interval,
                    options_default.label_insertion,
                    options_default.xpad_interval,
                    PADPacketizer::ALLOWED_PADLEN.c_str()
           );
}


static std::string list_dls_files(std::vector<std::string> dls_files) {
    std::string result = "";
    for (const std::string& dls_file : dls_files) {
        if (!result.empty())
            result += "/";
        result += "'" + dls_file + "'";
    }
    return result;
}


int main(int argc, char *argv[]) {
    // Version handling is done very early to ensure nothing else but the version gets printed out
    if (argc == 2 and strcmp(argv[1], "--version") == 0) {
        fprintf(stdout, "%s\n",
#if defined(GITVERSION)
                GITVERSION
#else
                PACKAGE_VERSION
#endif
               );
        return 0;
    }

    header();

    // get/check options
    PadEncoderOptions options;

    const struct option longopts[] = {
        {"charset",         required_argument,  0, 'c'},
        {"raw-dls",         no_argument,        0, 'C'},
        {"remove-dls",      no_argument,        0, 'r'},
        {"dir",             required_argument,  0, 'd'},
        {"erase",           no_argument,        0, 'e'},
        {"output",          required_argument,  0, 'o'},
        {"dls",             required_argument,  0, 't'},
        {"item-state",      required_argument,  0, 'I'},
        {"sleep",           required_argument,  0, 's'},
        {"max-slide-size",  required_argument,  0, 'm'},
        {"raw-slides",      no_argument,        0, 'R'},
        {"help",            no_argument,        0, 'h'},
        {"label",           required_argument,  0, 'l'},
        {"label-ins",       required_argument,  0, 'L'},
        {"xpad-interval",   required_argument,  0, 'X'},
        {"verbose",         no_argument,        0, 'v'},
        {"dump-current-slide",   required_argument, 0, 1},
        {"dump-completed-slide", required_argument, 0, 2},
        {0,0,0,0},
    };

    int ch;
    while((ch = getopt_long(argc, argv, "eChRrc:d:o:s:t:I:l:L:X:vm:", longopts, NULL)) != -1) {
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
                options.socket_ident = optarg;
                break;
            case 's':
                options.slide_interval = atoi(optarg);
                break;
            case 't':   // can be used more than once!
                options.dls_files.push_back(optarg);
                break;
            case 'I':
                options.item_state_file = optarg;
                break;
            case 'm':
                options.max_slide_size = atoi(optarg);
                break;
            case 'R':
                options.raw_slides = true;
                break;
            case 'l':
                options.label_interval = atoi(optarg);
                break;
            case 'L':
                options.label_insertion = atoi(optarg);
                break;
            case 'X':
                options.xpad_interval = atoi(optarg);
                break;
            case 'v':
                verbose++;
                break;
            case 1: // dump-current-slide
                options.current_slide_dump_name = optarg;
                break;
            case 2: // dump-completed-slide
                options.completed_slide_dump_name = optarg;
                break;
            case '?':
            case 'h':
                usage(argv[0]);
                return 0;
        }
    }

    if (options.max_slide_size > SLSEncoder::MAXSLIDESIZE_SIMPLE) {
        fprintf(stderr, "ODR-PadEnc Error: max slide size %zu exceeds Simple Profile limit %zu\n",
                options.max_slide_size, SLSEncoder::MAXSLIDESIZE_SIMPLE);
        return 2;
    }

    if (options.sls_dir && not options.dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' and DLS from %s to '%s'\n",
                options.sls_dir, list_dls_files(options.dls_files).c_str(), options.socket_ident.c_str());
    }
    else if (options.sls_dir) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' to '%s'. No DLS.\n",
                options.sls_dir, options.socket_ident.c_str());
    }
    else if (not options.dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding DLS from %s to '%s'. No Slideshow.\n",
                list_dls_files(options.dls_files).c_str(), options.socket_ident.c_str());
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

    if (options.item_state_file)
        fprintf(stderr, "ODR-PadEnc reading DL Plus Item Toggle/Running bits from '%s'.\n", options.item_state_file);


    // TODO: check uniform PAD encoder options!?

    if (options.xpad_interval < 1) {
        fprintf(stderr, "ODR-PadEnc Error: The X-PAD interval must be 1 or greater!\n");
        return 1;
    }

#if HAVE_MAGICKWAND
    MagickWandGenesis();
    if (verbose)
        fprintf(stderr, "ODR-PadEnc using ImageMagick version '%s'\n", GetMagickVersion(NULL));
#endif

    // handle signals
    if (signal(SIGINT, break_handler) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGINT handler");
        return 1;
    }
    if (signal(SIGTERM, break_handler) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGTERM handler");
        return 1;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGPIPE to be ignored");
        return 1;
    }

    int result = 0;

    PadInterface intf;
    try {
        intf.open(options.socket_ident);

        uint8_t previous_padlen = 0;

        std::shared_ptr<PadEncoder> pad_encoder;

        while (!do_exit) {
            options.padlen = intf.receive_request();

            if (options.padlen > 0) {
                if (previous_padlen != options.padlen) {
                    previous_padlen = options.padlen;

                    if (!PADPacketizer::CheckPADLen(options.padlen)) {
                        fprintf(stderr, "ODR-PadEnc Error: PAD length %d invalid: Possible values: %s\n",
                                options.padlen, PADPacketizer::ALLOWED_PADLEN.c_str());
                        result = 2;
                        break;
                    }
                    else {
                        fprintf(stderr, "ODR-PadEnc Reinitialise PAD length to %d\n", options.padlen);
                        pad_encoder = std::make_shared<PadEncoder>(options);
                    }
                }

                result = pad_encoder->Encode(intf);
                if (result > 0) {
                    break;
                }
            }
        }
    }
    catch (const std::runtime_error& e) {
        fprintf(stderr, "ODR-PadEnc failure: %s\n", e.what());
    }

#if HAVE_MAGICKWAND
    MagickWandTerminus();
#endif

    return result;
}


// --- PadEncoder -----------------------------------------------------------------
PadEncoder::PadEncoder(PadEncoderOptions options) :
        options(options),
        pad_packetizer(PADPacketizer(options.padlen)),
        dls_encoder(DLSEncoder(&pad_packetizer)),
        sls_encoder(SLSEncoder(&pad_packetizer)),
        slides_success(false),
        curr_dls_file(0)
{
    // PAD related timelines
    next_slide = next_label = next_label_insertion = steady_clock::now();

    // if multiple DLS files, ensure that initial increment leads to first one
    if (options.dls_files.size() > 1) {
        curr_dls_file = -1;
    }

    xpad_interval_counter = 0;
}


int PadEncoder::CheckRereadFile(const std::string& type, const std::string& path) {
    struct stat path_stat;
    if (stat(path.c_str(), &path_stat)) {
        // ignore missing request file
        if (errno != ENOENT) {
            perror(("ODR-PadEnc Error: could not retrieve " + type +" re-read request file stat").c_str());
            return -1;  // error
        }
        return 0;   // no re-read
    } else {
        // handle request
        fprintf(stderr, "ODR-PadEnc received %s re-read request!\n", type.c_str());
        if (unlink(path.c_str()))
            perror(("ODR-PadEnc Error: erasing file '" + path +"' failed").c_str());
        return 1;   // re-read
    }
}

int PadEncoder::EncodeSlide() {
    // skip insertion, if previous one not yet finished
    if (pad_packetizer.QueueContainsDG(SLSEncoder::APPTYPE_MOT_START)) {
        fprintf(stderr, "ODR-PadEnc Warning: skipping slide insertion, as previous one still in transmission!\n");
        return 0;
    }

    // check for slides dir re-read request
    int reread = CheckRereadFile("slides dir", std::string(options.sls_dir) + "/" + SLSEncoder::REQUEST_REREAD_FILENAME);
    switch (reread) {
    case 1:     // re-read requested
        slides.Clear();
        break;
    case -1:    // error
        return 1;
    }

    // usually invoked once
    for (;;) {
        // try to read slides dir (if present)
        if (slides.Empty()) {
            if (!slides.InitFromDir(options.sls_dir))
                return 1;
            slides_success = false;
        }

        // if slides available, encode the first one
        if (!slides.Empty()) {
            slide_metadata_t slide = slides.GetSlide();

            if (sls_encoder.encodeSlide(slide.filepath, slide.fidx, options.raw_slides, options.max_slide_size, options.current_slide_dump_name)) {
                slides_success = true;
                if (options.erase_after_tx) {
                    if (unlink(slide.filepath.c_str()))
                        perror(("ODR-PadEnc Error: erasing file '" + slide.filepath +"' failed").c_str());
                }
            } else {
                /* skip to next slide, except this is the last slide and so far
                 * no slide worked, to prevent an infinite loop and because
                 * re-reading the slides dir just moments later won't result in
                 * a different amount of slides. */
                bool skipping = !(slides.Empty() && !slides_success);
                fprintf(stderr, "ODR-PadEnc Error: cannot encode file '%s'; %s\n", slide.filepath.c_str(), skipping ? "skipping" : "giving up for now");
                if (skipping)
                    continue;
            }
        }

        break;
    }

    return 0;
}

int PadEncoder::EncodeLabel() {
    // skip insertion, if previous one not yet finished
    if (pad_packetizer.QueueContainsDG(DLSEncoder::APPTYPE_START)) {
        fprintf(stderr, "ODR-PadEnc Warning: skipping label insertion, as previous one still in transmission!\n");
    }
    else {
        dls_encoder.encodeLabel(options.dls_files[curr_dls_file], options.item_state_file, options.dl_params);
    }

    return 0;
}


int PadEncoder::Encode(PadInterface& intf) {
    steady_clock::time_point pad_timeline = std::chrono::steady_clock::now();

    int result = 0;

    // handle SLS
    if (options.SLSEnabled()) {

        // Check if slide transmission is complete
        if (    not options.completed_slide_dump_name.empty() and
                not options.current_slide_dump_name.empty() and
                not pad_packetizer.QueueContainsDG(SLSEncoder::APPTYPE_MOT_START)) {
            if (rename(options.current_slide_dump_name.c_str(), options.completed_slide_dump_name.c_str())) {
                if (errno != ENOENT) {
                    perror("ODR-PadEnc Error: renaming completed slide file failed");
                }
            }
            else {
                fprintf(stderr, "ODR-PadEnc completed slide transmission.\n");
            }
        }

        if (options.slide_interval > 0) {
            // encode slides regularly
            if (pad_timeline >= next_slide) {
                result = EncodeSlide();
                next_slide += std::chrono::seconds(options.slide_interval);
            }
        } else {
            // encode slide as soon as previous slide has been transmitted
            if (!pad_packetizer.QueueContainsDG(SLSEncoder::APPTYPE_MOT_START))
                result = EncodeSlide();
        }
    }
    if (result)
        return result;

    // handle DLS
    if (options.DLSEnabled()) {
        // check for DLS re-read request
        for (size_t i = 0; i < options.dls_files.size(); i++) {
            int reread = CheckRereadFile("DLS file '" + options.dls_files[i] + "'", options.dls_files[i] + DLSEncoder::REQUEST_REREAD_SUFFIX);
            switch (reread) {
            case 1:     // re-read requested
                // switch to desired DLS file
                curr_dls_file = i;
                next_label = pad_timeline + std::chrono::seconds(options.label_interval);

                // enforce label insertion
                next_label_insertion = pad_timeline;
                break;
            case -1:    // error
                return 1;
            }
        }

        if (options.dls_files.size() > 1 && pad_timeline >= next_label) {
            // switch to next DLS file
            curr_dls_file = (curr_dls_file + 1) % options.dls_files.size();
            next_label += std::chrono::seconds(options.label_interval);

            // enforce label insertion
            next_label_insertion = pad_timeline;
        }

        if (pad_timeline >= next_label_insertion) {
            // encode label
            result = EncodeLabel();
            next_label_insertion += std::chrono::milliseconds(options.label_insertion);
        }
    }
    if (result)
        return result;

    // flush one PAD (considering X-PAD output interval)
    auto pad = pad_packetizer.GetNextPAD(xpad_interval_counter == 0);

    intf.send_pad_data(pad.data(), pad.size());

    // update X-PAD output interval counter
    xpad_interval_counter = (xpad_interval_counter + 1) % options.xpad_interval;

    return 0;
}
