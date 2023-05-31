#include "external/dep1/dep1.h"

int main(int, char **) {
  dep1::f();
  dep1::C *c = dep1::newC();
  dep1::deleteC(c);
  dep1::S<int> s{};
  return s.identity(0);
}
