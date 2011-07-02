// { dg-xfail-if "BOGUS" { "*-*-*" } { "-fpph-map=pph.map" } }
// { dg-bogus "wchar.h:1:0: error: PPH file stdio.pph fails macro validation, _WCHAR_H" "" { xfail *-*-* } 0 }
// { dg-bogus "unistd.h:1144:34: error: declaration of .* ctermid.* has a different exception specifier" "" { xfail *-*-* } 0 }
// { dg-bogus "stdio.h:858:14: error: from previous declaration .* ctermid.*" "" { xfail *-*-* } 0 }

#ifndef X1DYNARRAY7_H
#define X1DYNARRAY7_H

#include <stddef.h>
#include <new>
#include <memory>
#include <stdexcept>
#include <iterator>

namespace tst {

template< typename T >
struct dynarray
{
    #include "a1dynarray-dcl1.hi"
    #include "a1dynarray-dcl2b.hi"
    #include "a1dynarray-dcl3.hi"
    #include "a1dynarray-dcl4.hi"
};

#include "a1dynarray-dfn1b.hi"
#include "a1dynarray-dfn2c.hi"
#include "a1dynarray-dfn3c.hi"

} // namespace tst

#endif
