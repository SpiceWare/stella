//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2017 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef BANK_ROM_CHEAT_HXX
#define BANK_ROM_CHEAT_HXX

#include "Cheat.hxx"

class BankRomCheat : public Cheat
{
  public:
    BankRomCheat(OSystem& os, const string& name, const string& code);
    virtual ~BankRomCheat() = default;

    bool enable() override;
    bool disable() override;
    void evaluate() override;

  private:
    uInt8  savedRom[16];
    uInt16 address;
    uInt8  value;
    uInt8  count;
    int    bank;

  private:
    // Following constructors and assignment operators not supported
    BankRomCheat() = delete;
    BankRomCheat(const BankRomCheat&) = delete;
    BankRomCheat(BankRomCheat&&) = delete;
    BankRomCheat& operator=(const BankRomCheat&) = delete;
    BankRomCheat& operator=(BankRomCheat&&) = delete;
};

#endif
