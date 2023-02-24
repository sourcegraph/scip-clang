// extra-args: -std=c++23

#include "macros.h"

int a = MY_MACRO + MY_MACRO_ALIAS;

#undef MY_MACRO

#if defined(MY_MACRO) // no reference
#endif

#define MY_MACRO 1

int b = MY_MACRO;

#if defined(MY_MACRO)
#endif

#ifdef MY_MACRO
#endif

#ifndef MY_MACRO
#endif

#ifdef NOT_YET_DEFINED_MACRO
#elifndef MY_MACRO
#elifdef MY_MACRO
#endif

#define NOT_YET_DEFINED_MACRO

#define IDENTITY_MACRO(__x) __x

int c = IDENTITY_MACRO(10);

#define MACRO_USING_MACRO \
  IDENTITY_MACRO(0)

// two uses of a macro that expands to another macro
int d = MACRO_USING_MACRO + MACRO_USING_MACRO;
