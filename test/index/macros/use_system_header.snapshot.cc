  #include "system_header.h"
//^ definition [..] `<file>/use_system_header.cc`/
//         ^^^^^^^^^^^^^^^^^ reference [..] `<file>/system_header.h`/
  
  const int total = SYSTEM_INT + OTHER_SYSTEM_INT;
//          ^^^^^ definition [..] total.
//                  ^^^^^^^^^^ reference [..] `system_header.h:3:9`!
//                               ^^^^^^^^^^^^^^^^ reference [..] `system_header.h:5:9`!
