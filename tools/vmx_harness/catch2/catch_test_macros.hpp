#pragma once
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace vmx_harness {
struct Test { const char* name; void (*run)(); };
inline std::vector<Test>& tests() { static std::vector<Test> value; return value; }
struct Register { Register(const char* name, void (*run)()) { tests().push_back({name, run}); } };
}
#define VMX_JOIN_IMPL(a,b) a##b
#define VMX_JOIN(a,b) VMX_JOIN_IMPL(a,b)
#define TEST_CASE(name,tags) \
  static void VMX_JOIN(vmx_test_,__LINE__)(); \
  static vmx_harness::Register VMX_JOIN(vmx_register_,__LINE__)(name,&VMX_JOIN(vmx_test_,__LINE__)); \
  static void VMX_JOIN(vmx_test_,__LINE__)()
#define REQUIRE(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)
