#ifndef SCIP_CLANG_CLANG_AST_MACROS_H
#define SCIP_CLANG_CLANG_AST_MACROS_H

#define FOR_EACH_DECL_TO_BE_INDEXED(F) \
  F(Binding)                           \
  F(EnumConstant)                      \
  F(Enum)                              \
  F(Field)                             \
  F(Function)                          \
  F(Namespace)                         \
  F(NonTypeTemplateParm)               \
  F(Record)                            \
  F(TemplateTemplateParm)              \
  F(TemplateTypeParm)                  \
  F(TypedefName)                       \
  F(Var)

#define FOR_EACH_TEMPLATE_PARM_TO_BE_INDEXED(F) \
  F(NonTypeTemplateParm)                        \
  F(TemplateTemplateParm)                       \
  F(TemplateTypeParm)

#define FOR_EACH_EXPR_TO_BE_INDEXED(F) \
  F(CXXConstruct)                      \
  F(DeclRef)                           \
  F(Member)

#define FOR_EACH_TYPE_TO_BE_INDEXED(F) \
  F(Enum)                              \
  F(Record)                            \
  F(TemplateSpecialization)            \
  F(TemplateTypeParm)

namespace clang {
#define FORWARD_DECLARE(DeclName) class DeclName##Decl;
FOR_EACH_DECL_TO_BE_INDEXED(FORWARD_DECLARE)
#undef FORWARD_DECLARE
} // namespace clang

#endif // SCIP_CLANG_CLANG_AST_MACROS_H