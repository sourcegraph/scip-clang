#include "api.h"
int use_apis() {
  CValue value{1};
  return c_api(value.value) + c_counter + cpp_api(2) + cpp_api(3.5)
       + nested::nested_api(4) + single_api(5);
}
