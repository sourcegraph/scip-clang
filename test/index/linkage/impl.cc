#include "api.h"
int c_api(int value) { return value; }
int c_counter = 7;
int cpp_api(int value) { return value; }
int cpp_api(double value) { return static_cast<int>(value); }
namespace nested {
int nested_api(int value) { return value; }
}
int single_api(int value) { return value; }
