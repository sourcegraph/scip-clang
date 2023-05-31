#include "myheader.h"
#include "ext_header.h"

void doStuff(S *, C *c) {
  f();
  c->m();
  if (Global == 1) {
    perform_magic();
  } else {
    undo_magic();
  }
}

int useExtern() {
  extern int externInt;
  return externInt;
}