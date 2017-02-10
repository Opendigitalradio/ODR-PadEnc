/*
    Copyright (C) 2017 Stefan Pöschel (http://opendigitalradio.org)

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
    \file common.cpp
    \brief Includes common settings/includes/etc.

    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "common.h"

int verbose = 0;

std::vector<std::string> split_string(const std::string &s, const char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string part;

    while (std::getline(ss, part, delimiter))
        result.push_back(part);
    return result;
}
