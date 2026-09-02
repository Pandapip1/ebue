// SPDX-FileCopyrightText: (C) 2026 Gavin John
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ExactCScalarSMT.h"

#include <cstdlib>
#include <iostream>

using ntlibc::algebra::CType;
using ntlibc::algebra::ScalarSMT;

static bool always(z3::context &Z, const z3::expr &Property) {
  z3::solver Solver(Z);
  Solver.add(!Property);
  return Solver.check() == z3::unsat;
}

static bool test(z3::context &Z, bool Condition, const char *Name) {
  if (Condition)
    return true;
  std::cerr << "exact-c-scalar-smt: failed: " << Name << '\n';
  return false;
}

int main() {
  z3::context Z;
  const CType SChar{8, 2, false};
  const CType UChar{8, 2, true};
  const CType Int{32, 4, false};
  const CType UInt{32, 4, true};
  ScalarSMT C(Z, Int, UInt);
  bool Okay = true;

  auto UMax = C.input(Z.bv_val(255, 8), UChar);
  auto UOne = C.input(Z.bv_val(1, 8), UChar);
  auto UWrapped = C.add(*UMax, *UOne);
  Okay &= test(Z,
               UWrapped && always(Z, UWrapped->Defined) &&
                   always(Z, !UWrapped->Events.UnsignedWrap) &&
                   always(Z, !UWrapped->Events.SignedOverflow) &&
                   always(Z, UWrapped->Value == Z.bv_val(256, 32)),
               "unsigned operands promote before addition");

  auto UConvertedWrap = C.addConverted(*UMax, *UOne);
  Okay &= test(Z,
               UConvertedWrap && UConvertedWrap->Type.sameDomain(UChar) &&
                   always(Z, UConvertedWrap->Defined) &&
                   always(Z, UConvertedWrap->Events.UnsignedWrap) &&
                   always(Z, UConvertedWrap->Value == Z.bv_val(0, 8)),
               "typed IR arithmetic does not repeat source promotions");

  auto UIntMax = C.input(Z.bv_val("4294967295", 32), UInt);
  auto UIntOne = C.input(Z.bv_val(1, 32), UInt);
  auto UIntWrapped = C.add(*UIntMax, *UIntOne);
  Okay &= test(Z,
               UIntWrapped && always(Z, UIntWrapped->Defined) &&
                   always(Z, UIntWrapped->Events.UnsignedWrap) &&
                   always(Z, UIntWrapped->Value == Z.bv_val(0, 32)),
               "defined unsigned wrap preserves modular value and event");

  auto IntMax = C.input(Z.bv_val("2147483647", 32), Int);
  auto IntOne = C.input(Z.bv_val(1, 32), Int);
  auto IntOverflow = C.add(*IntMax, *IntOne);
  Okay &= test(Z,
               IntOverflow && always(Z, !IntOverflow->Defined) &&
                   always(Z, IntOverflow->Events.SignedOverflow) &&
                   always(Z, !IntOverflow->Events.UnsignedWrap) &&
                   always(Z, IntOverflow->Value == Z.bv_val("2147483648", 32)),
               "signed overflow is undefined with an exact bit witness");

  auto Wide = C.input(Z.bv_val(256, 32), UInt);
  auto Narrowed = C.convert(*Wide, UChar);
  Okay &= test(Z,
               Narrowed && always(Z, Narrowed->Defined) &&
                   always(Z, Narrowed->Events.NarrowingLoss) &&
                   always(Z, Narrowed->Value == Z.bv_val(0, 8)),
               "defined narrowing records mathematical-value loss");

  auto Negative = C.input(Z.bv_val(255, 8), SChar);
  auto Promoted = C.promote(*Negative);
  Okay &= test(Z,
               Promoted && Promoted->Type.sameDomain(Int) &&
                   always(Z, Promoted->Value == Z.bv_val("4294967295", 32)) &&
                   always(Z, !Promoted->Events.NarrowingLoss),
               "integer promotion sign-extends without loss");

  auto ShiftCount = C.input(Z.bv_val(32, 32), Int);
  auto InvalidShift = C.shiftLeft(*IntOne, *ShiftCount);
  Okay &= test(Z,
               InvalidShift && always(Z, !InvalidShift->Defined) &&
                   always(Z, InvalidShift->Events.InvalidShift),
               "out-of-range shift is undefined and tagged");

  auto ThirtyOne = C.input(Z.bv_val(31, 32), Int);
  auto SignedShiftOverflow = C.shiftLeft(*IntOne, *ThirtyOne);
  Okay &=
      test(Z,
           SignedShiftOverflow && always(Z, !SignedShiftOverflow->Defined) &&
               always(Z, SignedShiftOverflow->Events.SignedOverflow),
           "signed left-shift overflow is undefined");

  auto Zero = C.input(Z.bv_val(0, 32), UInt);
  auto UnsignedUnderflow = C.subtract(*Zero, *UIntOne);
  Okay &= test(Z,
               UnsignedUnderflow && always(Z, UnsignedUnderflow->Defined) &&
                   always(Z, UnsignedUnderflow->Events.UnsignedWrap) &&
                   always(Z, UnsignedUnderflow->Value == UIntMax->Value),
               "unsigned subtraction underflow is a defined wrap event");

  auto UIntHigh = C.input(Z.bv_val("2147483648", 32), UInt);
  auto NarrowSigned = C.convert(*UIntHigh, Int);
  Okay &= test(Z,
               NarrowSigned && always(Z, NarrowSigned->Defined) &&
                   always(Z, NarrowSigned->Events.NarrowingLoss) &&
                   always(Z, NarrowSigned->Value == UIntHigh->Value),
               "same-width signed conversion preserves bits but records loss");

  auto NegativeShift = C.input(Z.bv_val("4294967295", 32), Int);
  auto InvalidNegativeShift = C.shiftLeft(*IntOne, *NegativeShift);
  Okay &=
      test(Z,
           InvalidNegativeShift && always(Z, !InvalidNegativeShift->Defined) &&
               always(Z, InvalidNegativeShift->Events.InvalidShift),
           "negative shift count remains invalid after bit encoding");

  auto UIntTopBit = C.input(Z.bv_val("2147483648", 32), UInt);
  auto UIntShift = C.shiftLeft(*UIntTopBit, *UIntOne);
  Okay &= test(Z,
               UIntShift && always(Z, UIntShift->Defined) &&
                   always(Z, UIntShift->Events.UnsignedWrap) &&
                   always(Z, UIntShift->Value == Z.bv_val(0, 32)),
               "unsigned left-shift loss is defined and tagged");

  auto IntTwo = C.input(Z.bv_val(2, 32), Int);
  auto IntProductOverflow = C.multiply(*IntMax, *IntTwo);
  Okay &= test(Z,
               IntProductOverflow && always(Z, !IntProductOverflow->Defined) &&
                   always(Z, IntProductOverflow->Events.SignedOverflow),
               "signed multiplication overflow is undefined");

  auto IntIncrement = C.unitStep(*IntMax, true);
  Okay &= test(Z,
               IntIncrement && always(Z, !IntIncrement->Defined) &&
                   always(Z, IntIncrement->Events.SignedOverflow) &&
                   always(Z, IntIncrement->Value == Z.bv_val("2147483648", 32)),
               "unit increment exposes the exact signed boundary event");

  auto UIntDecrement = C.unitStep(*Zero, false);
  Okay &= test(Z,
               UIntDecrement && always(Z, UIntDecrement->Defined) &&
                   always(Z, UIntDecrement->Events.UnsignedWrap) &&
                   always(Z, UIntDecrement->Value == UIntMax->Value),
               "unit decrement preserves defined unsigned wrapping");

  auto Mixed = C.add(*IntOne, *UIntMax);
  Okay &=
      test(Z,
           Mixed && Mixed->Type.sameDomain(UInt) && always(Z, Mixed->Defined) &&
               always(Z, Mixed->Events.UnsignedWrap) &&
               always(Z, Mixed->Value == Z.bv_val(0, 32)),
           "usual arithmetic conversions precede event classification");

  return Okay ? EXIT_SUCCESS : EXIT_FAILURE;
}
