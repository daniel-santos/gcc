/* { dg-do compile } */

#include <objc/objc.h>
#include "../../objc-obj-c++-shared/Object1.h"

@interface obj : Object {
@public 
  int var; 
} 
- (int) mth;
+ (id) dep_cls_mth __attribute__((deprecated)) ;/* { dg-warning "method attributes are not available in this version" } */
- (int) dep_ins_mth __attribute__((deprecated)) ;/* { dg-warning "method attributes are not available in this version" } */
- (int) dep_ins_mtharg: (int) i __attribute__((deprecated)) ;/* { dg-warning "method attributes are not available in this version" } */
- (int) dep_ins_mtharg1: (int) i __attribute__((deprecated)) add: (int) j;/* { dg-error "method attributes must be specified at the end " } */
- (int) nodef __attribute__((deprecated)) { return var-2; } ; /* { dg-error "expected ';' before '\{' token" } */
		/* { dg-warning "method attributes are not available in this version" "" { target *-*-* } 15 } */
__attribute__((deprecated)) 
- (int) bad_pref_mth; /* { dg-warning "prefix attributes are ignored for methods" } */
@end

@implementation obj
- (int) mth { return var; }
+ (id) dep_cls_mth { return self; }
- (int) dep_ins_mth  { return var ; }
- (int) dep_ins_mtharg: (int) i { return var + i ; }
- (int) dep_ins_mtharg1: (int) i add: (int) j { return var + i + j ; } 
- (int) bad_pref_mth { return var; };
- (int) nodef { return var-2; } ; 
@end 

int foo (void)
{
  obj *p = [obj new];
  id n = [obj dep_cls_mth];
  
  [p dep_ins_mth];
  [p dep_ins_mtharg:2];
  [p dep_ins_mtharg1:3 add:3];

  return [p mth];    
}
