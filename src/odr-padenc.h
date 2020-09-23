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
    \file odr-padenc.h
    \brief Generate PAD data for MOT Slideshow and DLS

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "common.h"

#include <atomic>
#include <stdlib.h>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include "pad_interface.h"
#include "pad_common.h"
#include "dls.h"
#include "sls.h"

using std::chrono::steady_clock;


// --- PadEncoderOptions -----------------------------------------------------------------
struct PadEncoderOptions {
    uint8_t padlen = 0;
    bool erase_after_tx = false;
    int slide_interval = 10;
    int label_interval = 12;    // uniform PAD encoder only
    int label_insertion = 1200; // uniform PAD encoder only
    int xpad_interval = 1;      // uniform PAD encoder only
    size_t max_slide_size = SLSEncoder::MAXSLIDESIZE_SIMPLE;
    bool raw_slides = false;
    DL_PARAMS dl_params;

    const char *sls_dir = nullptr;
    std::string socket_ident;
    std::vector<std::string> dls_files;
    const char *item_state_file = nullptr;
    std::string current_slide_dump_name;
    std::string completed_slide_dump_name;

    bool DLSEnabled() const { return !dls_files.empty(); }
    bool SLSEnabled() const { return sls_dir; }
};


// --- PadEncoder -----------------------------------------------------------------
class PadEncoder {
protected:
    PadEncoderOptions options;
    PADPacketizer pad_packetizer;
    DLSEncoder dls_encoder;
    SLSEncoder sls_encoder;
    SlideStore slides;
    bool slides_success;
    int curr_dls_file;
    steady_clock::time_point next_slide;
    steady_clock::time_point next_label;
    steady_clock::time_point next_label_insertion;
    size_t xpad_interval_counter;

    int EncodeSlide();
    int EncodeLabel();
    static int CheckRereadFile(const std::string& type, const std::string& path);

public:
    PadEncoder(PadEncoderOptions options);
    virtual ~PadEncoder() {}

    int Encode(PadInterface& intf);
};

