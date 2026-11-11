extern "C" {
int c_api(int value);
extern int c_counter;
struct CValue { int value; };
extern "C++" {
int cpp_api(int value);
int cpp_api(double value);
}
}
namespace nested {
extern "C" {
int nested_api(int value);
}
}
extern "C" int single_api(int value);
