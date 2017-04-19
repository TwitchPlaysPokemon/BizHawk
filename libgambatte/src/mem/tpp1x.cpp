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
#include "tpp1x.h"
#include "../savestate.h"
#include <cstdlib>

namespace gambatte {

Tpp1X::Tpp1X()
: baseTime(0)
, haltTime(0)
, rombank(1)
, rambank(0)
, dataW(0)
, dataH(0)
, dataM(0)
, dataS(0)
, rumble(0)
, curmap(0)
, features(0)
, enabled(false)
, running(false)
, overflow(false)
, timeCB(0)
{
}

void Tpp1X::latch() {
	std::uint32_t tmp = (running ? timeCB() : haltTime) - baseTime;

	while (tmp >= 154828800) {
		baseTime += 154828800;
		tmp -= 154828800;
		overflow = true;
	}

	dataW = tmp / 604800;
	tmp %= 604800;

	dataH = (tmp / 86400) << 5;
	tmp %= 86400;
	dataH |= tmp / 3600;
	tmp %= 3600;

	dataM = tmp / 60;
	tmp %= 60;

	dataS = tmp;
}

void Tpp1X::settime() {
	baseTime = running ? timeCB() : haltTime;
	baseTime -= dataS + (dataM * 60) + ((dataH & 0x1F) * 3600) + (((dataH & 0xE0) >> 5) * 86400) + (dataW * 604800);
}

void Tpp1X::halt() {
	if (running) {
		haltTime = timeCB();
		running = false;
	}
}

void Tpp1X::resume() {
	if (!running) {
		baseTime += timeCB() - haltTime;
		running = true;
	}
}

void Tpp1X::setRumble(unsigned char amount) {
	if (features & 1)
		rumble = features & 2 ? amount : amount > 0;
}

void Tpp1X::loadState(SaveState const &state) {
	baseTime = state.rtc.baseTime;
	haltTime = state.rtc.haltTime;
	curmap = state.rtc.dataDh & 0x0F;
	rumble = (state.rtc.dataDh & 0xF0) >> 4;
	dataW = state.rtc.dataDl;
	dataH = state.rtc.dataH;
	dataM = state.rtc.dataM;
	dataS = state.rtc.dataS;
	running = state.rtc.lastLatchData & 2;
	overflow = state.rtc.lastLatchData & 1;
}

unsigned char Tpp1X::read(unsigned p) const {
	switch (curmap) {
	case 0:
		switch (p & 3) {
		case 0: return rombank & 0x00FF;
		case 1: return (rombank & 0xFF00) >> 8;
		case 2: return rambank;
		case 3: return (rumble & 3) | (running << 2) | (overflow << 3) | 0xF0;
		}
	case 3:
		switch (p & 3) {
		case 0: return dataW;
		case 1: return dataH;
		case 2: return dataM;
		case 3: return dataS;
		}
	}
	return 0xFF;
}

void Tpp1X::write(unsigned p, unsigned data) {
	if (curmap == 3) { // only special map that can be written
		switch (p & 3) {
		case 0: dataW = data; break;
		case 1: dataH = data; break;
		case 2: dataM = data; break;
		case 3: dataS = data; break;
		}
	}
}

SYNCFUNC(Tpp1X)
{
	NSS(baseTime);
	NSS(haltTime);
	NSS(rombank);
	NSS(rambank);
	NSS(dataW);
	NSS(dataH);
	NSS(dataM);
	NSS(dataS);
	NSS(rumble);
	NSS(curmap);
	NSS(features);
	NSS(enabled);
	NSS(running);
	NSS(overflow);
}

}
