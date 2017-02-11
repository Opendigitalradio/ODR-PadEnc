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
    \file dls.h
    \brief Dynamic Label (DL) related code

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#ifndef DLS_H_
#define DLS_H_

#include <fstream>
#include <iostream>

#include "pad_common.h"
#include "charset.h"


// Charsets from TS 101 756
enum {
    CHARSET_COMPLETE_EBU_LATIN      =  0, //!< Complete EBU Latin based repertoire
    CHARSET_EBU_LATIN_CY_GR         =  1, //!< EBU Latin based common core, Cyrillic, Greek
    CHARSET_EBU_LATIN_AR_HE_CY_GR   =  2, //!< EBU Latin based core, Arabic, Hebrew, Cyrillic and Greek
    CHARSET_ISO_LATIN_ALPHABET_2    =  3, //!< ISO Latin Alphabet No 2
    CHARSET_UCS2_BE                 =  6, //!< ISO/IEC 10646 using UCS-2 transformation format, big endian byte order
    CHARSET_UTF8                    = 15  //!< ISO Latin Alphabet No 2
};

// DL/DL+ commands
enum {
    DLS_CMD_REMOVE_LABEL = 0b0001,
    DLS_CMD_DL_PLUS      = 0b0010
};
enum {
    DL_PLUS_CMD_TAGS     = 0b0000
};


// --- DL_PLUS_TAG -----------------------------------------------------------------
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


// --- DL_STATE -----------------------------------------------------------------
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


// --- DLSManager -----------------------------------------------------------------
class DLSManager {
private:
    DATA_GROUP* createDynamicLabelCommand(uint8_t command);
    DATA_GROUP* createDynamicLabelPlus(const DL_STATE& dl_state);
    bool parse_dl_param_bool(const std::string &key, const std::string &value, bool &target);
    bool parse_dl_param_int_dl_plus_tag(const std::string &key, const std::string &value, int &target);
    void parse_dl_params(std::ifstream &dls_fstream, DL_STATE &dl_state);
    int dls_count(const std::string& text);
    DATA_GROUP* dls_get(const std::string& text, uint8_t charset, int seg_index);
    void prepend_dl_dgs(PADPacketizer& pad_packetizer, const DL_STATE& dl_state, uint8_t charset);

    CharsetConverter charset_converter;
    bool dls_toggle;
    DL_STATE dl_state_prev;
public:
    static const size_t MAXDLS;
    static const size_t DLS_SEG_LEN_PREFIX;
    static const size_t DLS_SEG_LEN_CHAR_MAX;
    static const int DLS_REPETITION_WHILE_SLS;
    static const std::string DL_PARAMS_OPEN;
    static const std::string DL_PARAMS_CLOSE;

    DLSManager() : dls_toggle(false) {}
    void writeDLS(PADPacketizer& pad_packetizer, const std::string& dls_file, uint8_t charset, bool raw_dls, bool remove_dls);
};

#endif /* DLS_H_ */
