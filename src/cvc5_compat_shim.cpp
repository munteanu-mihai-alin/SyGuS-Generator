// ABI compatibility shim: cvc5 was built with GCC 10 which uses
// std::fpos<int> for seekpos, while GCC 15 uses std::fpos<__mbstate_t>.
// We provide the old-ABI symbol so the static library can link.

#include <streambuf>
#include <ios>

using pos_type = std::basic_streambuf<char, std::char_traits<char>>::pos_type;
using off_type = std::basic_streambuf<char, std::char_traits<char>>::off_type;

// The missing symbol is:
// std::basic_streambuf<char, std::char_traits<char>>::seekpos(std::fpos<int>, std::_Ios_Openmode)
// Mangled: _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekposESt4fposIiESt13_Ios_Openmode

extern "C"
pos_type _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekposESt4fposIiESt13_Ios_Openmode(
    void*, long long, int) {
  return pos_type(off_type(-1));
}
