/***************************************************************************
 *   Copyright (C) 2017 by TPPDevs                                         *
 *   tppdevs.com                                                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2 as     *
 *   published by the Free Software Foundation.                            *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License version 2 for more details.                *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   version 2 along with this program; if not, write to the               *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef TPP1X_H
#define TPP1X_H

#include <cstdint>
#include "newstate.h"

namespace gambatte {

struct SaveState;

class Tpp1X {
private:
	std::uint32_t baseTime;
	std::uint32_t haltTime;
	unsigned short rombank;
	unsigned char rambank;
	unsigned char dataW;
	unsigned char dataH;
	unsigned char dataM;
	unsigned char dataS;
	unsigned char rumble;
	unsigned char curmap;
	unsigned char features;
	bool enabled;
	bool running;
	bool overflow;
	std::uint32_t(*timeCB)();
	
public:
	Tpp1X();
	std::uint32_t getBaseTime() const { return baseTime; }
	void setBaseTime(std::uint32_t baseTime) { baseTime = baseTime; }
	void latch();
	void settime();
	void resetOverflow() { overflow = false;}
	void halt();
	void resume();
	void setRumble(unsigned char amount);
	void loadState(SaveState const &state);
	void set(bool enabled, unsigned char features) {
		enabled = enabled;
		features = features;
	}
	bool isTPP1() const { return enabled; }
	void setRombank(unsigned short rombank) { rombank = rombank; }
	void setRambank(unsigned char rambank) { rambank = rambank; }
	void setMap(unsigned char map) { curmap = map; }
	unsigned char getFeatures() const { return features; }
	unsigned char read(unsigned p) const;
	void write(unsigned p, unsigned data);
	void setRTCCallback(std::uint32_t(*callback)()) {
		timeCB = callback;
	}

	template<bool isReader>void SyncState(NewState *ns);
};

}

#endif
