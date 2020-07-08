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
    \file pad_common.cpp
    \brief Common parts related to PAD

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "pad_common.h"


// --- DATA_GROUP -----------------------------------------------------------------
DATA_GROUP::DATA_GROUP(size_t len, int apptype_start, int apptype_cont) {
    this->data.resize(len);
    this->apptype_start = apptype_start;
    this->apptype_cont = apptype_cont;
    written = 0;
}

void DATA_GROUP::AppendCRC() {
    uint16_t crc = 0xFFFF;
    crc = odr::crc16(crc, &data[0], data.size());
    crc = ~crc;
#ifdef DEBUG
    fprintf(stderr, "crc=%04x ~crc=%04x\n", crc, ~crc);
#endif

    data.push_back((crc & 0xFF00) >> 8);
    data.push_back((crc & 0x00FF));
}

size_t DATA_GROUP::Available() {
    return data.size() - written;
}

int DATA_GROUP::Write(uint8_t *write_data, size_t len, int *cont_apptype) {
    size_t written_now = std::min(len, Available());

    // fill up remaining bytes with zero padding
    memcpy(write_data, &data[written], written_now);
    memset(write_data + written_now, 0x00, len - written_now);

    // set app type depending on progress
    int apptype = written > 0 ? apptype_cont : apptype_start;

    written += written_now;

    // prevent continuation of a different DG having the same type
    if (cont_apptype)
        *cont_apptype = Available() > 0 ? apptype_cont : -1;

    return apptype;
}


// --- PADPacketizer -----------------------------------------------------------------
const size_t PADPacketizer::SUBFIELD_LENS[]     = {4, 6, 8, 12, 16, 24, 32, 48};
const size_t PADPacketizer::FPAD_LEN            =   2;
const size_t PADPacketizer::SHORT_PAD           =   6; // F-PAD + 1x CI              + 1x  3 bytes data sub-field
const size_t PADPacketizer::VARSIZE_PAD_MIN     =   8; // F-PAD + 1x CI + end marker + 1x  4 bytes data sub-field
const size_t PADPacketizer::VARSIZE_PAD_MAX     = 196; // F-PAD + 4x CI              + 4x 48 bytes data sub-field
const std::string PADPacketizer::ALLOWED_PADLEN = "6 (short X-PAD), 8 to 196 (variable size X-PAD)";
const int PADPacketizer::APPTYPE_DGLI = 1;

PADPacketizer::PADPacketizer(size_t pad_size) :
    xpad_size_max(pad_size - FPAD_LEN),
    short_xpad(pad_size == SHORT_PAD),
    max_cis(short_xpad ? 1 : 4),
    last_ci_type(-1)
{
    ResetPAD();
}

PADPacketizer::~PADPacketizer() {
    while (!queue.empty()) {
        delete queue.front();
        queue.pop_front();
    }
}

void PADPacketizer::AddDG(DATA_GROUP* dg, bool prepend) {
    queue.insert(prepend ? queue.begin() : queue.end(), dg);
}

void PADPacketizer::AddDGs(const std::vector<DATA_GROUP*>& dgs, bool prepend) {
    queue.insert(prepend ? queue.begin() : queue.end(), dgs.cbegin(), dgs.cend());
}

bool PADPacketizer::QueueFilled() {
    return !queue.empty();
}

bool PADPacketizer::QueueContainsDG(int apptype_start) {
    for (const DATA_GROUP* dg : queue)
        if (dg->apptype_start == apptype_start)
            return true;
    return false;
}

pad_t* PADPacketizer::GetPAD() {
    bool pad_flushable = false;

    // process DG queue
    while (!pad_flushable && !queue.empty()) {
        DATA_GROUP* dg = queue.front();

        // repeatedly append DG
        while (!pad_flushable && dg->Available() > 0)
            pad_flushable = AppendDG(dg);

        if (dg->Available() == 0) {
            delete dg;
            queue.pop_front();
        }
    }

    // (possibly empty) PAD
    return FlushPAD();
}

std::vector<uint8_t> PADPacketizer::GetNextPAD(bool output_xpad) {
    pad_t* pad = output_xpad ? GetPAD() : FlushPAD();

    if (verbose >= 2) {
        fprintf(stderr, "ODR-PadEnc writing PAD (%zu bytes):", pad->size());
        for (size_t j = 0; j < pad->size(); j++) {
            const char sep = (j == (pad->size() - 1) || j == (pad->size() - 1 - FPAD_LEN)) ? '|' : ' ';
            fprintf(stderr, "%c%02X", sep , (*pad)[j]);
        }
        fprintf(stderr, "\n");
    }

    std::vector<uint8_t> p = *pad;
    delete pad;
    return p;
}


size_t PADPacketizer::AddCINeededBytes() {
    // returns the amount of additional bytes needed for the next CI

    // special cases: end marker added/replaced
    if (!short_xpad && used_cis == 0)
        return 2;
    if (!short_xpad && used_cis == (max_cis - 1))
        return 0;
    return 1;
}

void PADPacketizer::AddCI(int apptype, int len_index) {
    ci_type[used_cis] = apptype;
    ci_len_index[used_cis] = len_index;

    xpad_size += AddCINeededBytes();
    used_cis++;
}


int PADPacketizer::OptimalSubFieldSizeIndex(size_t available_bytes) {
    /*! Return the index of the optimal sub-field size by stepwise search (regards only Variable Size X-PAD):
     * - find the smallest sub-field able to hold (at least) all available bytes
     * - find the biggest regarding sub-field we have space for (which definitely exists - otherwise previously the PAD would have been flushed)
     * - if the wasted space is at least as big as the smallest possible sub-field, use a sub-field one size smaller
     */
    int len_index = 0;

    while ((len_index + 1) < 8 && SUBFIELD_LENS[len_index] < available_bytes)
        len_index++;
    while ((len_index - 1) >= 0 && (SUBFIELD_LENS[len_index] + AddCINeededBytes()) > (xpad_size_max - xpad_size))
        len_index--;
    if ((len_index - 1) >= 0 && ((int) SUBFIELD_LENS[len_index] - (int) available_bytes) >= (int) SUBFIELD_LENS[0])
        len_index--;

    return len_index;
}

int PADPacketizer::WriteDGToSubField(DATA_GROUP* dg, size_t len) {
    int apptype = dg->Write(&subfields[subfields_size], len, &last_ci_type);
    subfields_size += len;
    xpad_size += len;
    return apptype;
}


bool PADPacketizer::AppendDG(DATA_GROUP* dg) {
    /*! use X-PAD w/o CIs instead of X-PAD w/ CIs, if we can save some bytes or at least do not waste additional bytes
     *
     * Omit CI list in case:
     * 1.   no pending data sub-fields
     * 2.   last CI type valid
     * 3.   last CI type matching current (continuity) CI type
     * 4a.  short X-PAD; OR
     * 4ba. size of the last X-PAD being at least as big as the available X-PAD payload in case all CIs are used AND
     * 4bb. the amount of available DG bytes being at least as big as the size of the last X-PAD in case all CIs are used
     */
    if (
            used_cis == 0 &&
            last_ci_type != -1 &&
            last_ci_type == dg->apptype_cont &&
            (short_xpad ||
                    (last_ci_size >= (xpad_size_max - max_cis) &&
                            dg->Available() >= (last_ci_size - max_cis)))
            ) {
        AppendDGWithoutCI(dg);
        return true;
    } else {
        AppendDGWithCI(dg);

        // if no further sub-fields could be added, PAD must be flushed
        if (used_cis == max_cis || SUBFIELD_LENS[0] + AddCINeededBytes() > (xpad_size_max - xpad_size))
            return true;
    }
    return false;
}


void PADPacketizer::AppendDGWithCI(DATA_GROUP* dg) {
    int len_index = short_xpad ? 0 : OptimalSubFieldSizeIndex(dg->Available());
    size_t len_size = short_xpad ? 3 : SUBFIELD_LENS[len_index];

    int apptype = WriteDGToSubField(dg, len_size);
    AddCI(apptype, len_index);

#ifdef DEBUG
    fprintf(stderr, "PADPacketizer: added sub-field w/  CI - type: %2d, size: %2zu\n", apptype, len_size);
#endif
}

void PADPacketizer::AppendDGWithoutCI(DATA_GROUP* dg) {
#ifdef DEBUG
    int old_last_ci_type = last_ci_type;
#endif

    WriteDGToSubField(dg, last_ci_size);

#ifdef DEBUG
    fprintf(stderr, "PADPacketizer: added sub-field w/o CI - type: %2d, size: %2zu\n", old_last_ci_type, last_ci_size);
#endif
}

void PADPacketizer::ResetPAD() {
    xpad_size = 0;
    subfields_size = 0;
    used_cis = 0;
}

pad_t* PADPacketizer::FlushPAD() {
    pad_t* result = new pad_t(xpad_size_max + FPAD_LEN + 1);
    pad_t &pad = *result;

    size_t pad_offset = xpad_size_max;

    if (subfields_size > 0) {
        if (used_cis > 0) {
            // X-PAD: CIs
            for (size_t i = 0; i < used_cis; i++)
                pad[--pad_offset] = (short_xpad ? 0 : ci_len_index[i]) << 5 | ci_type[i];

            // X-PAD: end marker (if needed)
            if (used_cis < max_cis)
                pad[--pad_offset] = 0x00;
        }

        // X-PAD: sub-fields (reversed on-the-fly)
        for (size_t off = 0; off < subfields_size; off++)
            pad[--pad_offset] = subfields[off];
    } else {
        // no X-PAD
        last_ci_type = -1;
    }

    // zero padding
    memset(&pad[0], 0x00, pad_offset);

    // F-PAD
    pad[xpad_size_max + 0] = subfields_size > 0 ? (short_xpad ? 0x10 : 0x20) : 0x00;
    pad[xpad_size_max + 1] = subfields_size > 0 ? (used_cis > 0 ? 0x02 : 0x00) : 0x00;

    // used PAD len
    pad[xpad_size_max + FPAD_LEN] = xpad_size + FPAD_LEN;

    last_ci_size = xpad_size;
    ResetPAD();
    return result;
}

DATA_GROUP* PADPacketizer::CreateDataGroupLengthIndicator(size_t len) {
    DATA_GROUP* dg = new DATA_GROUP(2, APPTYPE_DGLI, APPTYPE_DGLI);    // continuation never used (except for comparison at short X-PAD)
    uint8_vector_t &data = dg->data;

    // Data Group length
    data[0] = (len & 0x3F00) >> 8;
    data[1] = (len & 0x00FF);

    // CRC
    dg->AppendCRC();

    return dg;
}

bool PADPacketizer::CheckPADLen(size_t len) {
    return len == PADPacketizer::SHORT_PAD || (len >= PADPacketizer::VARSIZE_PAD_MIN && len <= PADPacketizer::VARSIZE_PAD_MAX);
}
