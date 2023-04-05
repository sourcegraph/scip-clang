  #pragma GCC system_header
//^ definition [..] `<file>/system_header.h`/
  
  #define SYSTEM_INT 0
//        ^^^^^^^^^^ definition [..] `system_header.h:3:9`!
  
  #define OTHER_SYSTEM_INT (SYSTEM_INT + 1)
//        ^^^^^^^^^^^^^^^^ definition [..] `system_header.h:5:9`!
//                          ^^^^^^^^^^ reference [..] `system_header.h:3:9`!
