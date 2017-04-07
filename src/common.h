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
    \file common.h
    \brief Includes common settings/includes/etc.

    \author Stefan Pöschel <odr@basicmaster.de>
*/

#ifndef COMMON_H_
#define COMMON_H_

// enable for debug output
//#define DEBUG

// include settings by configure
#ifdef HAVE_CONFIG_H
#   include "config.h"
#endif


// ANSI colors
#define ODR_COLOR_DL    "\x1B[36m"  // DL text
#define ODR_COLOR_SLS   "\x1B[33m"  // SLS image
#define ODR_COLOR_RST   "\x1B[0m"   // reset


#include <string>
#include <vector>
#include <sstream>
#include <stdio.h>


extern int verbose;
extern std::vector<std::string> split_string(const std::string &s, const char delimiter);

#endif /* COMMON_H_ */
