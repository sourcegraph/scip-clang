struct MonoBase {};

struct MonoDerived: MonoBase {};

template <typename T>
struct TemplatedBase {};

template <typename T>
struct TemplatedDerived: TemplatedBase<T> {};

struct DerivedFromInstantiation: TemplatedBase<int> {};

template <typename T>
struct SpecializedBase {};

template <>
struct SpecializedBase<int> {};

template <typename T>
struct SpecializedDerived: SpecializedBase<T> {};

struct DerivedFromSpecialization: SpecializedBase<int> {};

template <typename T>
struct CrtpBase { T *t; };

struct CrtpDerivedMono: CrtpBase<CrtpDerivedMono> {};

template <typename T>
struct CrtpDerivedTemplated: CrtpBase<CrtpDerivedTemplated<T>> {};

template <typename T>
struct DerivedFromTemplateParam: T {};

template <template <typename> typename H>
struct DerivedFromTemplateTemplateParam: H<int> {};