  
// definition [..] `<file>/types.h`/
  namespace has_anon_enum {
//          ^^^^^^^^^^^^^ definition [..] has_anon_enum/
//          kind Namespace
    enum { E1, E2 } e = E1;
//  ^^^^ definition [..] has_anon_enum/$anonymous_type_a58c49fe3a7ec9aa_0#
//  kind Enum
//         ^^ definition [..] has_anon_enum/E1.
//         kind EnumMember
//             ^^ definition [..] has_anon_enum/E2.
//             kind EnumMember
//                  ^ definition [..] has_anon_enum/e.
//                  kind Variable
//                      ^^ reference [..] has_anon_enum/E1.
  }
