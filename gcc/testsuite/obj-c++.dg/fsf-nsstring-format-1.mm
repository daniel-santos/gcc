/* Check NSString format extensions.  */
/* { dg-do compile { target *-*-darwin* } } */
/* { dg-skip-if "" { *-*-* } { "-fgnu-runtime" } { "" } } */
/* { dg-options "-Wall" } */

#ifndef __CONSTANT_CFSTRINGS__
#error requires CFString
#endif

#ifdef __cplusplus
extern "C" {
#endif
extern int printf (const char *fmt, ...);
typedef const struct __CFString * CFStringRef;

#ifdef __cplusplus
}
#endif

@class NSString;

int s1 (NSString *fmt, ...) __attribute__((format(NSString, 1, 2))) ; /* OK */
/* A CFString can represent an NSString.  */
int s1a (CFStringRef fmt, ...) __attribute__((format(NSString, 1, 2))) ; /* OK */
/* But... it is possible that a CFString format might imply functionality that
   is not present in objective-c.  */
int s1b (NSString *fmt, ...) __attribute__((format(CFString, 1, 2))) ; /* { dg-error "format argument should be a .CFString. reference" } */

int s2 (int a, NSString *fmt, ... ) __attribute__((format(__NSString__, 2, 3))) ; /* OK */

int s2a (int a, NSString *fmt, ... ) __attribute__((format(NSString, 2, 2))) ; /* { dg-error "format string argument follows the args to be formatted" } */

int s3 (const char *fmt, ... ) __attribute__((format(__NSString__, 1, 2))) ; /* { dg-error "format argument should be a .NSString. reference but a string was found" } */
int s4 (NSString *fmt, ... ) __attribute__((format(printf, 1, 2))) ; /* { dg-error "found a .NSString. reference but the format argument should be a string" } */

char *s5 (char dum, char *fmt1, ... ) __attribute__((format_arg(2))) ; /* OK */
NSString *s6 (NSString *dum, NSString *fmt1, ... ) __attribute__((format_arg(2))) ; /* OK */

char *s7 (int dum, void *fmt1, ... ) __attribute__((format_arg(2))) ; /* { dg-error "format string argument is not a string type" } */
int s8 (NSString *dum, NSString *fmt1, ... ) __attribute__((format_arg(2))) ; /* { dg-error "function does not return string type" } */

char *s9 (int dum, char *fmt1, ... ) __attribute__((format_arg(2))) ; /* OK */
NSString *s10 (int dum, NSString *fmt1, ... ) __attribute__((format_arg(2))) ; /* OK */

void foo (void)
{
  s1 (@"this format not checked %d %s", 3, 4);
  printf("this one is checked %d %s", 3, 4, 5); /* { dg-warning "format .%s. expects type .char.., but argument 3 has type 'int'" } */
			/* { dg-warning "too many arguments for format" "" { target *-*-* } 48 } */
  printf(s9 (1, (char *)"and so is this %d %d %s" , 3, 4, "hm"), 5, 6, 12); /* { dg-warning "format .%s. expects type .char.., but argument 4 has type .int." } */
}
