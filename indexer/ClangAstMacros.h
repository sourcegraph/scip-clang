#ifndef SCIP_CLANG_CLANG_AST_MACROS_H
#define SCIP_CLANG_CLANG_AST_MACROS_H

#define FOR_EACH_DECL_TO_BE_INDEXED(F) \
  F(Binding)                           \
  F(ClassTemplate)                     \
  F(EnumConstant)                      \
  F(Enum)                              \
  F(Field)                             \
  F(Function)                          \
  F(FunctionTemplate)                  \
  F(Namespace)                         \
  F(NonTypeTemplateParm)               \
  F(Record)                            \
  F(TemplateTemplateParm)              \
  F(TemplateTypeParm)                  \
  F(TypeAliasTemplate)                 \
  F(TypedefName)                       \
  F(UsingShadow)                       \
  F(Using)                             \
  F(Var)                               \
  F(VarTemplate)

#define FOR_EACH_TEMPLATE_PARM_TO_BE_INDEXED(F) \
  F(NonTypeTemplateParm)                        \
  F(TemplateTemplateParm)                       \
  F(TemplateTypeParm)

#define FOR_EACH_EXPR_TO_BE_INDEXED(F) \
  F(CXXConstruct)                      \
  F(CXXDependentScopeMember)           \
  F(DeclRef)                           \
  F(Member)                            \
  F(UnresolvedMember)

#define FOR_EACH_TYPE_TO_BE_INDEXED(F) \
  F(Enum)                              \
  F(Record)                            \
  F(TemplateSpecialization)            \
  F(TemplateTypeParm)                  \
  F(Typedef)                           \
  F(Using)

namespace clang {
#define FORWARD_DECLARE(DeclName) class DeclName##Decl;
FOR_EACH_DECL_TO_BE_INDEXED(FORWARD_DECLARE)
#undef FORWARD_DECLARE
} // namespace clang

#endif // SCIP_CLANG_CLANG_AST_MACROS_H