// { dg-do run }
// { dg-xfail-if "BOGUS" { "*-*-*" } { "-fpph-map=pph.map" } }

#include "x1dynarray1.h"

typedef int integer;
typedef dynarray< integer > integer_array;

int main()
{
    #include "a1dynarray-use1.cci"
    #include "a1dynarray-use2.cci"
    #include "a1dynarray-use4a.cci"
    return sum - 25;
}
