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
#include "config.h"
#include "pad_interface.h"
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

#define MESSAGE_REQUEST 1
#define MESSAGE_PAD_DATA 2

using namespace std;

void PadInterface::open(const std::string &pad_ident)
{
    m_pad_ident = pad_ident;

    m_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_sock == -1) {
        throw runtime_error("PAD socket creation failed: " + string(strerror(errno)));
    }

    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "/tmp/%s.padenc", m_pad_ident.c_str());
    if (unlink(claddr.sun_path) == -1 and errno != ENOENT) {
        fprintf(stderr, "Unlinking of socket %s failed: %s\n", claddr.sun_path, strerror(errno));
    }

    int ret = bind(m_sock, (const struct sockaddr *) &claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        throw runtime_error("PAD socket bind failed " + string(strerror(errno)));
    }
}

uint8_t PadInterface::receive_request()
{
    if (m_pad_ident.empty()) {
        throw logic_error("Uninitialised PadInterface::request() called");
    }

    vector<uint8_t> buffer(4);

    while (true) {
        struct pollfd fds[1];
        fds[0].fd = m_sock;
        fds[0].events = POLLIN;
        int timeout_ms = 240;

        int retval = poll(fds, 1, timeout_ms);

        if (retval == -1) {
            std::string errstr(strerror(errno));
            throw std::runtime_error("PAD socket poll error: " + errstr);
        }
        else if (retval > 0) {
            ssize_t ret = recvfrom(m_sock, buffer.data(), buffer.size(), 0, nullptr, nullptr);

            if (ret == -1) {
                throw runtime_error(string("Can't receive data: ") + strerror(errno));
            }
            else {
                buffer.resize(ret);

                // We could check where the data comes from, but since we're using UNIX sockets
                // the source is anyway local to the machine.

                if (buffer[0] == MESSAGE_REQUEST) {
                    uint8_t padlen = buffer[1];
                    return padlen;
                }
                else {
                    continue;
                }
            }
        }
        else {
            return 0;
        }
    }
}

void PadInterface::send_pad_data(const uint8_t *data, size_t len)
{
    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "/tmp/%s.audioenc", m_pad_ident.c_str());

    vector<uint8_t> message(len + 1);
    message[0] = MESSAGE_PAD_DATA;
    copy(data, data + len, message.begin() + 1);

    ssize_t ret = sendto(m_sock, message.data(), message.size(), 0, (struct sockaddr*)&claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        // This suppresses the -Wlogical-op warning
        if (errno == EAGAIN
#if EAGAIN != EWOULDBLOCK
                or errno == EWOULDBLOCK
#endif
                or errno == ECONNREFUSED
                or errno == ENOENT) {
            if (m_audioenc_reachable) {
                fprintf(stderr, "ODR-PadEnc at %s not reachable\n", claddr.sun_path);
                m_audioenc_reachable = false;
            }
        }
        else {
            fprintf(stderr, "PAD send failed: %s\n", strerror(errno));
        }
    }
    else if ((size_t)ret != message.size()) {
        fprintf(stderr, "PAD incorrect length sent: %zu bytes of %zu transmitted\n", ret, len);
    }
    else if (not m_audioenc_reachable) {
        fprintf(stderr, "Audio encoder is now reachable at %s\n", claddr.sun_path);
        m_audioenc_reachable = true;
    }
}
