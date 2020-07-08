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

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>

/*! \file PadInterface.h
 *
 * Handles communication with ODR-PadEnc using a socket
 */

class PadInterface {
    public:
        /*! Create a new PAD data interface that binds to /tmp/pad_ident.padenc and
         * communicates with ODR-AudioEnc at /tmp/pad_ident.audioenc
         */
        void open(const std::string &pad_ident);

        /*! Receives a request from the audio encoder
         *
         * \return the desired padlen
         */
        uint8_t receive_request();

        void send_pad_data(const uint8_t *data, size_t len);

    private:
        std::string m_pad_ident;
        int m_sock = -1;
        bool m_audioenc_reachable = true;
};
