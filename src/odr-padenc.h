/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014, 2015 Matthias P. Braendli (http://opendigitalradio.org)

    Copyright (C) 2015, 2016, 2017, 2018 Stefan Pöschel (http://opendigitalradio.org)

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

#include "pad_common.h"
#include "dls.h"
#include "sls.h"

using std::chrono::steady_clock;


// --- PadEncoderOptions -----------------------------------------------------------------
struct PadEncoderOptions {
    size_t padlen;
    bool erase_after_tx;
    int slide_interval;
    int frame_dur;          // uniform PAD encoder only
    int label_interval;     // uniform PAD encoder only
    int label_insertion;    // uniform PAD encoder only
    int init_burst;         // uniform PAD encoder only
    bool raw_slides;
    DL_PARAMS dl_params;

    const char* sls_dir;
    const char* output;
    std::vector<std::string> dls_files;

    PadEncoderOptions() :
            padlen(58),
            erase_after_tx(false),
            slide_interval(10),
            frame_dur(0),
            label_interval(12),
            label_insertion(1200),
            init_burst(12),
            raw_slides(false),
            sls_dir(NULL),
            output("/tmp/pad.fifo")
    {}

    bool DLSEnabled() {return !dls_files.empty();}
    bool SLSEnabled() {return sls_dir;}
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
    int output_fd;
    steady_clock::time_point run_timeline;

    std::atomic<bool> do_exit;

    PadEncoder(PadEncoderOptions options) :
        options(options),
        pad_packetizer(PADPacketizer(options.padlen)),
        dls_encoder(DLSEncoder(&pad_packetizer)),
        sls_encoder(SLSEncoder(&pad_packetizer)),
        slides_success(false),
        curr_dls_file(0),
        output_fd(-1),
        run_timeline(steady_clock::now()),
        do_exit(false)
    {}

    virtual int Encode() = 0;
    int EncodeSlide(bool skip_if_already_queued);
    int EncodeLabel(bool skip_if_already_queued);
public:
    virtual ~PadEncoder() {}

    int Main();
    void DoExit() {do_exit = true;}
};


// --- BurstPadEncoder -----------------------------------------------------------------
class BurstPadEncoder : public PadEncoder {
private:
    static const int DLS_REPETITION_WHILE_SLS;

    int Encode();
public:
    BurstPadEncoder(PadEncoderOptions options) : PadEncoder(options) {}
};


// --- UniformPadEncoder -----------------------------------------------------------------
class UniformPadEncoder : public PadEncoder {
private:
    steady_clock::time_point pad_timeline;
    steady_clock::time_point next_slide;
    steady_clock::time_point next_label;
    steady_clock::time_point next_label_insertion;

    int Encode();
public:
    UniformPadEncoder(PadEncoderOptions options);
};
