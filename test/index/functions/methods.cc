// Virtual methods and inheritance

struct S0 {
  virtual void v1() {
    v2();
  }
  virtual void v2() {}
  virtual void v3() {}
  virtual void v4() {}
  virtual void v5() = 0;

};

struct S1_0: S0 {
  virtual void v1() override { v2(); }
  virtual void v2() /*override*/ {}
};

struct S1_1: S0 {
  virtual void v2() override { v1(); }
  virtual void v3() override { v5(); }
};

struct S2 final: S1_0, S1_1 {
  virtual void v1() override { v5(); }
  virtual void v2() override {}
  virtual void v3() override {}
  virtual void v4() override {}
  virtual void v5() override {}
};

// Method forward declarations

struct A0 {
  void f();
  static void g();
  struct B0 {
    void f();
    static void g();
  };
  friend bool operator==(const A0 &, const A0 &);
};

void A0::f() {}
void A0::g() {}
void A0::B0::f() {}
void A0::B0::g() {}

// Not A::operator==
bool operator==(const A0 &, const A0 &) { return true; }

// Static methods

struct Z0 {
  static void f();
  void g() { f(); }
};

struct Z1: Z0 {
  void h() {
    f();
    Z1::f();
    Z0::f();
  }
};

// Member function pointer

struct M0 {
  void f() {}
};

void test_member_pointer() {
  void (M0::*p)() = &M0::f;
  M0 m{};
  (m.*p)();
}

// Using-declarations making methods public:

namespace u {
  struct Z0 {
  protected:
    void f(int) {}
    void f(int, int) {}
  };

  struct Z1: Z0 {
    using Z0::f;
  };

  void use_made_public(Z1 z1) {
    z1.f(0);
    z1.f(0, 0);
  }
}
