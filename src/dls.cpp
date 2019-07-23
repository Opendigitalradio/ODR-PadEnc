/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014, 2015 Matthias P. Braendli (http://opendigitalradio.org)

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
    \file dls.cpp
    \brief Dynamic Label (DL) related code

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "dls.h"


// --- DLSEncoder -----------------------------------------------------------------
const size_t DLSEncoder::MAXDLS = 128; // chars
const size_t DLSEncoder::DLS_SEG_LEN_PREFIX = 2;
const size_t DLSEncoder::DLS_SEG_LEN_CHAR_MAX = 16;
const std::string DLSEncoder::DL_PARAMS_OPEN  = "##### parameters { #####";
const std::string DLSEncoder::DL_PARAMS_CLOSE = "##### parameters } #####";
const int DLSEncoder::APPTYPE_START = 2;
const int DLSEncoder::APPTYPE_CONT = 3;
const std::string DLSEncoder::REQUEST_REREAD_SUFFIX = ".REQUEST_DLS_REREAD";



DATA_GROUP* DLSEncoder::createDynamicLabelCommand(uint8_t command) {
    DATA_GROUP* dg = new DATA_GROUP(2, APPTYPE_START, APPTYPE_CONT);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg + last seg + command flag + command
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (1 << 6) +
            (1 << 5) +
            (1 << 4) +
            command;

    // prefix: reserved
    seg_data[1] = 0;

    // CRC
    dg->AppendCRC();

    return dg;
}

DATA_GROUP* DLSEncoder::createDynamicLabelPlus(const DL_STATE& dl_state) {
    size_t tags_size = dl_state.dl_plus_tags.size();
    size_t len_dl_plus_cmd_field = 1 + 3 * tags_size;
    DATA_GROUP* dg = new DATA_GROUP(2 + len_dl_plus_cmd_field, APPTYPE_START, APPTYPE_CONT);
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


bool DLSEncoder::parse_dl_param_bool(const std::string &key, const std::string &value, bool &target) {
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

bool DLSEncoder::parse_dl_param_int_dl_plus_tag(const std::string &key, const std::string &value, int &target) {
    int value_int = atoi(value.c_str());
    if (value_int >= 0x00 && value_int <= 0x7F) {
        target = value_int;
        return true;
    }
    fprintf(stderr, "ODR-PadEnc Warning: DL Plus tag parameter '%s' %d out of range - ignored\n", key.c_str(), value_int);
    return false;
}

void DLSEncoder::parse_dl_params(std::ifstream &dls_fstream, DL_STATE &dl_state) {
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
                dl_state.dl_plus_tags.emplace_back(content_type, start_marker, length_marker);
            continue;
        }

        fprintf(stderr, "ODR-PadEnc Warning: DL parameter '%s' unknown - ignored\n", key.c_str());
    }

    fprintf(stderr, "ODR-PadEnc Warning: no param closing tag, so the DLS text will be empty\n");
}


bool DLSEncoder::parseLabel(const std::string& dls_file, const DL_PARAMS& dl_params, DL_STATE& dl_state) {
    std::vector<std::string> dls_lines;

    std::ifstream dls_fstream(dls_file);
    if (!dls_fstream.is_open()) {
        std::cerr << "Could not open " << dls_file << std::endl;
        return false;
    }

    std::string line;
    // Read and convert lines one by one because the converter doesn't understand
    // line endings
    while (std::getline(dls_fstream, line)) {
        if (line.empty())
            continue;
        if (line == DL_PARAMS_OPEN) {
            parse_dl_params(dls_fstream, dl_state);
        } else {
            if (not dl_params.raw_dls && dl_params.charset == DABCharset::UTF8) {
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
            if (dl_params.charset == DABCharset::UCS2_BE)
                ss << '\0' << '\n';
            else
                ss << '\n';
        }

        // UCS-2 BE: if from file the first byte of \0\n remains, remove it
        if (dl_params.charset == DABCharset::UCS2_BE && dls_lines[i].size() % 2) {
            dls_lines[i].resize(dls_lines[i].size() - 1);
        }

        ss << dls_lines[i];
    }

    dl_state.dl_text = ss.str();
    if (dl_state.dl_text.size() > MAXDLS) {
        fprintf(stderr, "ODR-PadEnc Warning: oversized DLS text (%zu chars) had to be shortened\n", dl_state.dl_text.size());
        dl_state.dl_text.resize(MAXDLS);
    }

    return true;
}


void DLSEncoder::encodeLabel(const std::string& dls_file, const char* item_state_file, const DL_PARAMS& dl_params) {
    DL_STATE dl_state;
    if (!parseLabel(dls_file, dl_params, dl_state))
        return;

    // if enabled, derive DL Plus Item Toggle/Running bits from separate file
    if (item_state_file) {
        DL_STATE item_state;
        if (!parseLabel(item_state_file, DL_PARAMS(), item_state))
            return;

        dl_state.dl_plus_enabled = true;
        dl_state.dl_plus_item_toggle = item_state.dl_plus_item_toggle;
        dl_state.dl_plus_item_running = item_state.dl_plus_item_running;
    }

    // if DL Plus enabled, but no DL Plus tags were added, add the required DUMMY tag
    if (dl_state.dl_plus_enabled && dl_state.dl_plus_tags.empty())
        dl_state.dl_plus_tags.emplace_back();

    // toggle the toggle bit only on new DL state
    bool dl_state_is_new = dl_state != dl_state_prev;
    if (verbose) {
        fprintf(stderr, "ODR-PadEnc writing %s DLS text \"" ODR_COLOR_DL "%s" ODR_COLOR_RST "\"\n", dl_state_is_new ? "new" : "old", dl_state.dl_text.c_str());
        if (dl_state.dl_plus_enabled) {
            fprintf(
                    stderr, "ODR-PadEnc writing %s DL Plus tags (IT/IR: %d/%d): ",
                    dl_state_is_new ? "new" : "old",
                    dl_state.dl_plus_item_toggle ? 1 : 0,
                    dl_state.dl_plus_item_running ? 1 : 0);
            for (dl_plus_tags_t::const_iterator it = dl_state.dl_plus_tags.begin(); it != dl_state.dl_plus_tags.end(); it++) {
                if (it != dl_state.dl_plus_tags.begin())
                    fprintf(stderr, ", ");
                if (it->content_type == 0 && it->start_marker == 0 && it->length_marker == 0)
                    fprintf(stderr, "(DUMMY)");
                else
                    fprintf(stderr, "%d (S/L: %d/%d)", it->content_type, it->start_marker, it->length_marker);
            }
            fprintf(stderr, "\n");
        }
    }

    DATA_GROUP *remove_label_dg = NULL;
    if (dl_state_is_new) {
        if (dl_params.remove_dls)
            remove_label_dg = createDynamicLabelCommand(DLS_CMD_REMOVE_LABEL);

        dls_toggle = !dls_toggle;   // indicate changed text

        dl_state_prev = dl_state;
    }

    prepend_dl_dgs(dl_state, dl_params.raw_dls ? dl_params.charset : DABCharset::COMPLETE_EBU_LATIN);
    if (remove_label_dg)
        pad_packetizer->AddDG(remove_label_dg, true);
}


int DLSEncoder::dls_count(const std::string& text) {
    size_t text_len = text.size();
    return text_len / DLS_SEG_LEN_CHAR_MAX + (text_len % DLS_SEG_LEN_CHAR_MAX ? 1 : 0);
}


DATA_GROUP* DLSEncoder::dls_get(const std::string& text, DABCharset charset, int seg_index) {
    bool first_seg = seg_index == 0;
    bool last_seg  = seg_index == dls_count(text) - 1;

    int seg_text_offset = seg_index * DLS_SEG_LEN_CHAR_MAX;
    const char *seg_text_start = text.c_str() + seg_text_offset;
    size_t seg_text_len = std::min(text.size() - seg_text_offset, DLS_SEG_LEN_CHAR_MAX);

    DATA_GROUP* dg = new DATA_GROUP(DLS_SEG_LEN_PREFIX + seg_text_len, APPTYPE_START, APPTYPE_CONT);
    uint8_vector_t &seg_data = dg->data;

    // prefix: toggle? + first seg? + last seg? + (seg len - 1)
    seg_data[0] =
            (dls_toggle ? (1 << 7) : 0) +
            (first_seg  ? (1 << 6) : 0) +
            (last_seg   ? (1 << 5) : 0) +
            (seg_text_len - 1);

    // prefix: charset / seg index
    seg_data[1] = (first_seg ? (uint8_t) charset : seg_index) << 4;

    // character field
    memcpy(&seg_data[DLS_SEG_LEN_PREFIX], seg_text_start, seg_text_len);

    // CRC
    dg->AppendCRC();

#ifdef DEBUG
    fprintf(stderr, "DL segment:");
    for (const uint8_t& b : seg_data)
        fprintf(stderr, " %02x", b);
    fprintf(stderr, "\n");
#endif
    return dg;
}


void DLSEncoder::prepend_dl_dgs(const DL_STATE& dl_state, DABCharset charset) {
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
    fprintf(stderr, "DLS text: %s\n", dl_state.dl_text.c_str());
    fprintf(stderr, "Number of DL segments: %d\n", seg_count);
#endif
}
