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
    \file odr-padenc.h
    \brief Generate PAD data for MOT Slideshow and DLS

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "common.h"

#include <mutex>
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


// --- PadEncoderOptions -----------------------------------------------------------------
struct PadEncoderOptions {
    size_t padlen;
    bool erase_after_tx;
    int sleepdelay;
    bool raw_slides;
    DL_PARAMS dl_params;

    const char* sls_dir;
    const char* output;
    std::vector<std::string> dls_files;

    PadEncoderOptions() :
            padlen(58),
            erase_after_tx(false),
            sleepdelay(10),
            raw_slides(false),
            sls_dir(NULL),
            output("/tmp/pad.fifo")
    {}
};


// --- PadEncoder -----------------------------------------------------------------
class PadEncoder {
private:
    static const int DLS_REPETITION_WHILE_SLS;

    PadEncoderOptions options;

    std::mutex status_mutex;
    bool do_exit;
public:
    PadEncoder(PadEncoderOptions options) : options(options), do_exit(false) {}

    int Main();
    void DoExit();
};