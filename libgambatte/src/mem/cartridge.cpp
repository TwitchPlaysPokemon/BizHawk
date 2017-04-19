/***************************************************************************
 *   Copyright (C) 2007-2010 by Sindre Aamås                               *
 *   aamas@stud.ntnu.no                                                    *
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
#include "cartridge.h"
#include "../savestate.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace gambatte {

namespace {

static unsigned toMulti64Rombank(const unsigned rombank) {
	return (rombank >> 1 & 0x30) | (rombank & 0xF);
}

class DefaultMbc : public Mbc {
public:
	virtual bool isAddressWithinAreaRombankCanBeMappedTo(unsigned addr, unsigned bank) const {
		return (addr< 0x4000) == (bank == 0);
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
	}
};

class Mbc0 : public DefaultMbc {
	MemPtrs &memptrs;
	bool enableRam;

public:
	explicit Mbc0(MemPtrs &memptrs)
	: memptrs(memptrs),
	  enableRam(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		if (P < 0x2000) {
			enableRam = (data & 0xF) == 0xA;
			memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, 0);
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.enableRam = enableRam;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		enableRam = ss.enableRam;
		memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, 0);
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(enableRam);
	}
};

static inline unsigned rambanks(const MemPtrs &memptrs) {
	return static_cast<std::size_t>(memptrs.rambankdataend() - memptrs.rambankdata()) / 0x2000;
}

static inline unsigned rombanks(const MemPtrs &memptrs) {
	return static_cast<std::size_t>(memptrs.romdataend()     - memptrs.romdata()    ) / 0x4000;
}

class Mbc1 : public DefaultMbc {
	MemPtrs &memptrs;
	unsigned char rombank;
	unsigned char rambank;
	bool enableRam;
	bool rambankMode;

	static unsigned adjustedRombank(unsigned bank) { return bank & 0x1F ? bank : bank | 1; }
	void setRambank() const { memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, rambank & (rambanks(memptrs) - 1)); }
	void setRombank() const { memptrs.setRombank(adjustedRombank(rombank & (rombanks(memptrs) - 1))); }

public:
	explicit Mbc1(MemPtrs &memptrs)
	: memptrs(memptrs),
	  rombank(1),
	  rambank(0),
	  enableRam(false),
	  rambankMode(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		switch (P >> 13 & 3) {
		case 0:
			enableRam = (data & 0xF) == 0xA;
			setRambank();
			break;
		case 1:
			rombank = rambankMode ? data & 0x1F : (rombank & 0x60) | (data & 0x1F);
			setRombank();
			break;
		case 2:
			if (rambankMode) {
				rambank = data & 3;
				setRambank();
			} else {
				rombank = (data << 5 & 0x60) | (rombank & 0x1F);
				setRombank();
			}

			break;
		case 3:
			// Pretty sure this should take effect immediately, but I have a policy not to change old behavior
			// unless I have something (eg. a verified test or a game) that justifies it.
			rambankMode = data & 1;
			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.rambank = rambank;
		ss.enableRam = enableRam;
		ss.rambankMode = rambankMode;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		rombank = ss.rombank;
		rambank = ss.rambank;
		enableRam = ss.enableRam;
		rambankMode = ss.rambankMode;
		setRambank();
		setRombank();
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(rambank);
		NSS(enableRam);
		NSS(rambankMode);
	}
};

class Mbc1Multi64 : public Mbc {
	MemPtrs &memptrs;
	unsigned char rombank;
	bool enableRam;
	bool rombank0Mode;

	static unsigned adjustedRombank(unsigned bank) { return bank & 0x1F ? bank : bank | 1; }

	void setRombank() const {
		if (rombank0Mode) {
			const unsigned rb = toMulti64Rombank(rombank);
			memptrs.setRombank0(rb & 0x30);
			memptrs.setRombank(adjustedRombank(rb));
		} else {
			memptrs.setRombank0(0);
			memptrs.setRombank(adjustedRombank(rombank & (rombanks(memptrs) - 1)));
		}
	}

public:
	explicit Mbc1Multi64(MemPtrs &memptrs)
	: memptrs(memptrs),
	  rombank(1),
	  enableRam(false),
	  rombank0Mode(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		switch (P >> 13 & 3) {
		case 0:
			enableRam = (data & 0xF) == 0xA;
			memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, 0);
			break;
		case 1:
			rombank = (rombank   & 0x60) | (data    & 0x1F);
			memptrs.setRombank(adjustedRombank(rombank0Mode ? toMulti64Rombank(rombank) : rombank & (rombanks(memptrs) - 1)));
			break;
		case 2:
			rombank = (data << 5 & 0x60) | (rombank & 0x1F);
			setRombank();
			break;
		case 3:
			rombank0Mode = data & 1;
			setRombank();
			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.enableRam = enableRam;
		ss.rambankMode = rombank0Mode;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		rombank = ss.rombank;
		enableRam = ss.enableRam;
		rombank0Mode = ss.rambankMode;
		memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, 0);
		setRombank();
	}
	
	virtual bool isAddressWithinAreaRombankCanBeMappedTo(unsigned addr, unsigned bank) const {
		return (addr < 0x4000) == ((bank & 0xF) == 0);
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(enableRam);
		NSS(rombank0Mode);
	}
};

class Mbc2 : public DefaultMbc {
	MemPtrs &memptrs;
	unsigned char rombank;
	bool enableRam;

public:
	explicit Mbc2(MemPtrs &memptrs)
	: memptrs(memptrs),
	  rombank(1),
	  enableRam(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		switch (P & 0x6100) {
		case 0x0000:
			enableRam = (data & 0xF) == 0xA;
			memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, 0);
			break;
		case 0x2100:
			rombank = data & 0xF;
			memptrs.setRombank(rombank & (rombanks(memptrs) - 1));
			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.enableRam = enableRam;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		rombank = ss.rombank;
		enableRam = ss.enableRam;
		memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, 0);
		memptrs.setRombank(rombank & (rombanks(memptrs) - 1));
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(enableRam);
	}
};

class Mbc3 : public DefaultMbc {
	MemPtrs &memptrs;
	Rtc *const rtc;
	unsigned char rombank;
	unsigned char rambank;
	bool enableRam;

	static unsigned adjustedRombank(unsigned bank) { return bank & 0x7F ? bank : bank | 1; }
	void setRambank() const {
		unsigned flags = enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0;
		
		if (rtc) {
			rtc->set(enableRam, rambank);
			
			if (rtc->getActive())
				flags |= MemPtrs::RTC_EN;
		}

		memptrs.setRambank(flags, rambank & (rambanks(memptrs) - 1));
	}
	// we adjust the rombank before masking with size?  this seems correct, as how would the mbc
	// know that high rom address outputs were not connected
	void setRombank() const { memptrs.setRombank(adjustedRombank(rombank) & (rombanks(memptrs) - 1)); }

public:
	Mbc3(MemPtrs &memptrs, Rtc *const rtc)
	: memptrs(memptrs),
	  rtc(rtc),
	  rombank(1),
	  rambank(0),
	  enableRam(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		switch (P >> 13 & 3) {
		case 0:
			enableRam = (data & 0xF) == 0xA;
			setRambank();
			break;
		case 1:
			rombank = data & 0x7F;
			setRombank();
			break;
		case 2:
			rambank = data;
			setRambank();
			break;
		case 3:
			if (rtc)
				rtc->latch(data);

			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.rambank = rambank;
		ss.enableRam = enableRam;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		rombank = ss.rombank;
		rambank = ss.rambank;
		enableRam = ss.enableRam;
		setRambank();
		setRombank();
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(rambank);
		NSS(enableRam);
	}
};

class HuC1 : public DefaultMbc {
	MemPtrs &memptrs;
	unsigned char rombank;
	unsigned char rambank;
	bool enableRam;
	bool rambankMode;

	void setRambank() const {
		memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : MemPtrs::READ_EN,
		                   rambankMode ? rambank & (rambanks(memptrs) - 1) : 0);
	}

	void setRombank() const { memptrs.setRombank((rambankMode ? rombank : rambank << 6 | rombank) & (rombanks(memptrs) - 1)); }

public:
	explicit HuC1(MemPtrs &memptrs)
	: memptrs(memptrs),
	  rombank(1),
	  rambank(0),
	  enableRam(false),
	  rambankMode(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		switch (P >> 13 & 3) {
		case 0:
			enableRam = (data & 0xF) == 0xA;
			setRambank();
			break;
		case 1:
			rombank = data & 0x3F;
			setRombank();
			break;
		case 2:
			rambank = data & 3;
			rambankMode ? setRambank() : setRombank();
			break;
		case 3:
			rambankMode = data & 1;
			setRambank();
			setRombank();
			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.rambank = rambank;
		ss.enableRam = enableRam;
		ss.rambankMode = rambankMode;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		rombank = ss.rombank;
		rambank = ss.rambank;
		enableRam = ss.enableRam;
		rambankMode = ss.rambankMode;
		setRambank();
		setRombank();
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(rambank);
		NSS(enableRam);
		NSS(rambankMode);
	}
};

class Mbc5 : public DefaultMbc {
	MemPtrs &memptrs;
	unsigned short rombank;
	unsigned char rambank;
	bool enableRam;

	static unsigned adjustedRombank(const unsigned bank) { return bank; }
	void setRambank() const { memptrs.setRambank(enableRam ? MemPtrs::READ_EN | MemPtrs::WRITE_EN : 0, rambank & (rambanks(memptrs) - 1)); }
	void setRombank() const { memptrs.setRombank(adjustedRombank(rombank & (rombanks(memptrs) - 1))); }

public:
	explicit Mbc5(MemPtrs &memptrs)
	: memptrs(memptrs),
	  rombank(1),
	  rambank(0),
	  enableRam(false)
	{
	}

	virtual void romWrite(const unsigned P, const unsigned data) {
		switch (P >> 13 & 3) {
		case 0:
			enableRam = (data & 0xF) == 0xA;
			setRambank();
			break;
		case 1:
			rombank = P < 0x3000 ? (rombank   & 0x100) |  data
			                     : (data << 8 & 0x100) | (rombank & 0xFF);
			setRombank();
			break;
		case 2:
			rambank = data & 0xF;
			setRambank();
			break;
		case 3:
			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.rambank = rambank;
		ss.enableRam = enableRam;
	}

	virtual void loadState(const SaveState::Mem &ss) {
		rombank = ss.rombank;
		rambank = ss.rambank;
		enableRam = ss.enableRam;
		setRambank();
		setRombank();
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(rambank);
		NSS(enableRam);
	}
};

class Tpp1 : public DefaultMbc {
	MemPtrs &memptrs;
	Tpp1X *const tpp1x;
	unsigned short rombank;
	unsigned char rambank;
	unsigned char mapmode;

	void setRambank() const {
		unsigned flags = 0;
		switch (mapmode) {
		case 0: flags = MemPtrs::READ_EN | MemPtrs::RTC_EN; break;
		case 1: flags = MemPtrs::READ_EN; break;
		case 2: flags = MemPtrs::READ_EN | MemPtrs::WRITE_EN; break;
		case 3: flags = tpp1x->getFeatures() & 4 ? MemPtrs::READ_EN | MemPtrs::WRITE_EN | MemPtrs::RTC_EN : 0; break;
		}
		tpp1x->setRambank(rambank);
		memptrs.setRambank(flags, rambank & (rambanks(memptrs) - 1));
	}

	void setRombank() const {
		tpp1x->setRombank(rombank);
		memptrs.setRombank(rombank & (rombanks(memptrs) - 1));
	}

public:
	explicit Tpp1(MemPtrs &memptrs, Tpp1X *const tpp1x)
	: memptrs(memptrs)
	, tpp1x(tpp1x)
	, rombank(1)
	, rambank(0)
	, mapmode(0)
	{
	}

	virtual void romWrite(unsigned const p, unsigned const data) {
		if (p >= 0x4000) return;
		switch (p & 3) {
		case 0: // MR0
			rombank = (rombank & 0xFF00) | data;
			setRombank();
			break;
		case 1: // MR1
			rombank = (rombank & 0x00FF) | (data << 8);
			setRombank();
			break;
		case 2: // MR2 (SRAM)
			rambank = data;
			setRambank();
			break;
		case 3: // MR3
			switch(data) {
			case 0x00: mapmode = 0; tpp1x->setMap(0); setRambank(); break;
 			case 0x02: mapmode = 1; tpp1x->setMap(1); setRambank(); break;
 			case 0x03: mapmode = 2; tpp1x->setMap(2); setRambank(); break;
 			case 0x05: mapmode = 3; tpp1x->setMap(3); setRambank(); break;
			case 0x10: tpp1x->latch(); break;
			case 0x11: tpp1x->settime(); break;
			case 0x14: tpp1x->resetOverflow(); break;
			case 0x18: tpp1x->halt(); break;
			case 0x19: tpp1x->resume(); break;
			case 0x20:
			case 0x21:
			case 0x22:
			case 0x23: tpp1x->setRumble(data & 3); break;
			}
			break;
		}
	}

	virtual void saveState(SaveState::Mem &ss) const {
		ss.rombank = rombank;
		ss.rambank = rambank;
	}

	virtual void loadState(SaveState::Mem const &ss) {
		rombank = ss.rombank;
		rambank = ss.rambank;
		setRambank();
		setRombank();
	}

	virtual void SyncState(NewState *ns, bool isReader)
	{
		NSS(rombank);
		NSS(rambank);
		NSS(mapmode);
	}
};

static bool checkTPP1(unsigned char * header) {
	return header[0x0147] == 0xBC && header[0x0149] == 0xC1 && header[0x014A] == 0x65;
}

static bool hasRtc(unsigned char * header) {
	if (checkTPP1(header)) return header[0x153] & 4;
	switch (header[0x0147]) {
	case 0x0F:
	case 0x10: return true;
	default: return false;
	}
}

}

void Cartridge::setStatePtrs(SaveState &state) {
	state.mem.vram.set(memptrs.vramdata(), memptrs.vramdataend() - memptrs.vramdata());
	state.mem.sram.set(memptrs.rambankdata(), memptrs.rambankdataend() - memptrs.rambankdata());
	state.mem.wram.set(memptrs.wramdata(0), memptrs.wramdataend() - memptrs.wramdata(0));
}

void Cartridge::loadState(const SaveState &state) {
	if (tpp1x.isTPP1()) tpp1x.loadState(state);
	else rtc.loadState(state);
	mbc->loadState(state.mem);
}

static void enforce8bit(unsigned char *data, unsigned long sz) {
	if (static_cast<unsigned char>(0x100))
		while (sz--)
			*data++ &= 0xFF;
}

static unsigned pow2ceil(unsigned n) {
	--n;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	++n;

	return n;
}

int Cartridge::loadROM(const char *romfiledata, unsigned romfilelength, const bool forceDmg, const bool multicartCompat) {
	//const std::auto_ptr<File> rom(newFileInstance(romfile));

	//if (rom->fail())
	//	return -1;
	
	unsigned rambanks = 1;
	unsigned rombanks = 2;
	bool cgb = false;
	enum Cartridgetype { PLAIN, MBC1, MBC2, MBC3, MBC5, HUC1, TPP1 } type = PLAIN;

	{
		unsigned char header[0x154];
		//rom->read(reinterpret_cast<char*>(header), sizeof header);
		if (romfilelength >= sizeof header)
			std::memcpy(header, romfiledata, sizeof header);
		else
			return -1;

		switch (header[0x0147]) {
		case 0x00: std::puts("Plain ROM loaded."); type = PLAIN; break;
		case 0x01: std::puts("MBC1 ROM loaded."); type = MBC1; break;
		case 0x02: std::puts("MBC1 ROM+RAM loaded."); type = MBC1; break;
		case 0x03: std::puts("MBC1 ROM+RAM+BATTERY loaded."); type = MBC1; break;
		case 0x05: std::puts("MBC2 ROM loaded."); type = MBC2; break;
		case 0x06: std::puts("MBC2 ROM+BATTERY loaded."); type = MBC2; break;
		case 0x08: std::puts("Plain ROM with additional RAM loaded."); type = PLAIN; break;
		case 0x09: std::puts("Plain ROM with additional RAM and Battery loaded."); type = PLAIN; break;
		case 0x0B: std::puts("MM01 ROM not supported."); return -1;
		case 0x0C: std::puts("MM01 ROM not supported."); return -1;
		case 0x0D: std::puts("MM01 ROM not supported."); return -1;
		case 0x0F: std::puts("MBC3 ROM+TIMER+BATTERY loaded."); type = MBC3; break;
		case 0x10: std::puts("MBC3 ROM+TIMER+RAM+BATTERY loaded."); type = MBC3; break;
		case 0x11: std::puts("MBC3 ROM loaded."); type = MBC3; break;
		case 0x12: std::puts("MBC3 ROM+RAM loaded."); type = MBC3; break;
		case 0x13: std::puts("MBC3 ROM+RAM+BATTERY loaded."); type = MBC3; break;
		case 0x15: std::puts("MBC4 ROM not supported."); return -1;
		case 0x16: std::puts("MBC4 ROM not supported."); return -1;
		case 0x17: std::puts("MBC4 ROM not supported."); return -1;
		case 0x19: std::puts("MBC5 ROM loaded."); type = MBC5; break;
		case 0x1A: std::puts("MBC5 ROM+RAM loaded."); type = MBC5; break;
		case 0x1B: std::puts("MBC5 ROM+RAM+BATTERY loaded."); type = MBC5; break;
		case 0x1C: std::puts("MBC5+RUMBLE ROM not supported."); type = MBC5; break;
		case 0x1D: std::puts("MBC5+RUMBLE+RAM ROM not suported."); type = MBC5; break;
		case 0x1E: std::puts("MBC5+RUMBLE+RAM+BATTERY ROM not supported."); type = MBC5; break;
		case 0xBC:
			if (checkTPP1(header)) { std::puts("TPP1 ROM loaded."); type = TPP1; break; }
			else { std::puts("Wrong data-format, corrupt or unsupported ROM."); return -1; } // broken TPP1 header
		case 0xFC: std::puts("Pocket Camera ROM not supported."); return -1;
		case 0xFD: std::puts("Bandai TAMA5 ROM not supported."); return -1;
		case 0xFE: std::puts("HuC3 ROM not supported."); return -1;
		case 0xFF: std::puts("HuC1 ROM+RAM+BATTERY loaded."); type = HUC1; break;
		default: std::puts("Wrong data-format, corrupt or unsupported ROM."); return -1;
		}

		/*switch (header[0x0148]) {
		case 0x00: rombanks = 2; break;
		case 0x01: rombanks = 4; break;
		case 0x02: rombanks = 8; break;
		case 0x03: rombanks = 16; break;
		case 0x04: rombanks = 32; break;
		case 0x05: rombanks = 64; break;
		case 0x06: rombanks = 128; break;
		case 0x07: rombanks = 256; break;
		case 0x08: rombanks = 512; break;
		case 0x52: rombanks = 72; break;
		case 0x53: rombanks = 80; break;
		case 0x54: rombanks = 96; break;
		default: return -1;
		}

		std::printf("rombanks: %u\n", rombanks);*/

		if (type == TPP1) {
			if(header[0x152] == 0) rambanks = 0;
			else rambanks = 1 << std::max(header[0x152] - 1, 8);
		}
		else {
			switch (header[0x0149]) {
			case 0x00: /*std::puts("No RAM");*/ rambanks = type == MBC2; break;
			case 0x01: /*std::puts("2kB RAM");*/ /*rambankrom=1; break;*/
			case 0x02: /*std::puts("8kB RAM");*/
				rambanks = 1;
				break;
			case 0x03: /*std::puts("32kB RAM");*/
				rambanks = 4;
				break;
			case 0x04: /*std::puts("128kB RAM");*/
				rambanks = 16;
				break;
			case 0x05: /*std::puts("undocumented kB RAM");*/
				rambanks = 16;
				break;
			default: /*std::puts("Wrong data-format, corrupt or unsupported ROM loaded.");*/
				rambanks = 16;
				break;
			}
		}
		
		cgb = header[0x0143] >> 7 & (1 ^ forceDmg);
		std::printf("cgb: %d\n", cgb);
	}

	std::printf("rambanks: %u\n", rambanks);

	const std::size_t filesize = romfilelength; //rom->size();
	rombanks = std::max(pow2ceil(filesize / 0x4000), 2u);
	std::printf("rombanks: %u\n", static_cast<unsigned>(filesize / 0x4000));
	
	mbc.reset();
	memptrs.reset(rombanks, rambanks, cgb ? 8 : 2);
	rtc.set(false, 0);
	tpp1x.set(false, 0);

	//rom->rewind();
	//rom->read(reinterpret_cast<char*>(memptrs.romdata()), (filesize / 0x4000) * 0x4000ul);
	std::memcpy(memptrs.romdata(), romfiledata, (filesize / 0x4000) * 0x4000ul);
	std::memset(memptrs.romdata() + (filesize / 0x4000) * 0x4000ul, 0xFF, (rombanks - filesize / 0x4000) * 0x4000ul);
	enforce8bit(memptrs.romdata(), rombanks * 0x4000ul);
	
	//if (rom->fail())
	//	return -1;
	
	switch (type) {
	case PLAIN: mbc.reset(new Mbc0(memptrs)); break;
	case MBC1:
		if (!rambanks && rombanks == 64 && multicartCompat) {
			std::puts("Multi-ROM \"MBC1\" presumed");
			mbc.reset(new Mbc1Multi64(memptrs));
		} else
			mbc.reset(new Mbc1(memptrs));

		break;
	case MBC2: mbc.reset(new Mbc2(memptrs)); break;
	case MBC3: mbc.reset(new Mbc3(memptrs, hasRtc(memptrs.romdata()) ? &rtc : 0)); break;
	case MBC5: mbc.reset(new Mbc5(memptrs)); break;
	case HUC1: mbc.reset(new HuC1(memptrs)); break;
	case TPP1:
		tpp1x.set(true, memptrs.romdata()[0x153]);
		mbc.reset(new Tpp1(memptrs, &tpp1x));
		break;
	}

	return 0;
}

static bool hasBattery(unsigned char * header) {
	if (checkTPP1(header)) return header[0x0153] & 8;
	switch (header[0x0147]) {
	case 0x03:
	case 0x06:
	case 0x09:
	case 0x0F:
	case 0x10:
	case 0x13:
	case 0x1B:
	case 0x1E:
	case 0xFF: return true;
	default: return false;
	}
}

void Cartridge::loadSavedata(const char *data) {
	if (hasBattery(memptrs.romdata())) {
		int length = memptrs.rambankdataend() - memptrs.rambankdata();
		std::memcpy(memptrs.rambankdata(), data, length);
		data += length;
		enforce8bit(memptrs.rambankdata(), length);
	}

	if (hasRtc(memptrs.romdata())) {
		unsigned long basetime;
		std::memcpy(&basetime, data, 4);
		if (checkTPP1(memptrs.romdata())) tpp1x.setBaseTime(basetime);
		else rtc.setBaseTime(basetime);
	}
}

int Cartridge::saveSavedataLength() {
	int ret = 0;
	if (hasBattery(memptrs.romdata())) {
		ret = memptrs.rambankdataend() - memptrs.rambankdata();
	}
	if (hasRtc(memptrs.romdata())) {
		ret += 4;
	}
	return ret;
}

void Cartridge::saveSavedata(char *dest) {
	if (hasBattery(memptrs.romdata())) {
		int length = memptrs.rambankdataend() - memptrs.rambankdata();
		std::memcpy(dest, memptrs.rambankdata(), length);
		dest += length;
	}

	if (hasRtc(memptrs.romdata())) {
		const unsigned long basetime = checkTPP1(memptrs.romdata()) ? tpp1x.getBaseTime() : rtc.getBaseTime();
		std::memcpy(dest, &basetime, 4);
	}
}

bool Cartridge::getMemoryArea(int which, unsigned char **data, int *length) const {
	if (!data || !length)
		return false;

	switch (which)
	{
	case 0:
		*data = memptrs.vramdata();
		*length = memptrs.vramdataend() - memptrs.vramdata();
		return true;
	case 1:
		*data = memptrs.romdata();
		*length = memptrs.romdataend() - memptrs.romdata();
		return true;
	case 2:
		*data = memptrs.wramdata(0);
		*length = memptrs.wramdataend() - memptrs.wramdata(0);
		return true;
	case 3:
		*data = memptrs.rambankdata();
		*length = memptrs.rambankdataend() - memptrs.rambankdata();
		return true;

	default:
		return false;
	}
	return false;
}

SYNCFUNC(Cartridge)
{
	SSS(memptrs);
	SSS(rtc);
	SSS(tpp1x);
	TSS(mbc);
}

}
