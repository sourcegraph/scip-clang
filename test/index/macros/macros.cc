#include "macros.h"

int a = MY_MACRO;

#undef MY_MACRO

#define MY_MACRO 1

int b = MY_MACRO;

#ifdef MY_MACRO
#endif

#ifndef MY_MACRO
#endif

#ifdef NOT_YET_DEFINED_MACRO
#endif

#define NOT_YET_DEFINED_MACRO

#define IDENTITY_MACRO(__x) __x

int c = IDENTITY_MACRO(10);

#define MACRO_USING_MACRO \
  IDENTITY_MACRO(0)

int d = MACRO_USING_MACRO;
