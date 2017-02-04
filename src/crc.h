/*
   Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009 Her Majesty the
   Queen in Right of Canada (Communications Research Center Canada)

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

#include "common.h"

#include <stdint.h>

namespace odr{

	// The init functions can be used to create a new table with a different polynome
	void init_crc8tab(uint8_t l_code, uint8_t l_init);
	
	uint8_t crc8(uint8_t l_crc, const void *lp_data, unsigned l_nb);

	void init_crc16tab(uint16_t l_code, uint16_t l_init);
	uint16_t crc16(uint16_t l_crc, const void *lp_data, unsigned l_nb);
	
	void init_crc32tab(uint32_t l_code, uint32_t l_init);
	uint32_t crc32(uint32_t l_crc, const void *lp_data, unsigned l_nb);
	
}
