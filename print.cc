#include "print.hh"

#include <unistd.h>
#include <cstdio>

void write_cerr(Str str) {
  write(1, str.base, str.size);
}

template <class T>
void do_snprint(Print& s, u32 max, char const* fmt, T const& arg) {
  u32 cur = s.chars.size;
  s.chars.expand(cur + max);
  auto it = s.chars.begin() + cur;
  i32 n_written = snprintf(it, max, fmt, arg);
  check(n_written >= 0);
  s.chars.size += static_cast<u32>(n_written);
}

void print(u8 x, Print& s) { do_snprint(s, 4, "%u", u32(x)); }

void print(u16 x, Print& s) { do_snprint(s, 6, "%u", u32(x)); }

void print(u32 x, Print& s) { do_snprint(s, 11, "%u", x); }

void print(u64 x, Print& s) {
  do_snprint(s, 21, "%llu", (unsigned long long) x);
}

void print(i8 x, Print& s) { do_snprint(s, 5, "%d", i32(x)); }

void print(i16 x, Print& s) { do_snprint(s, 7, "%d", i32(x)); }

void print(i32 x, Print& s) { do_snprint(s, 12, "%d", x); }

void print(i64 x, Print& s) { do_snprint(s, 22, "%lld", (long long) x); }

void print(void* x, Print& s) {
  do_snprint(s, 3 + 2 * sizeof(void*), "%p", x);
}

void print_array(
    char const* base, u32 size, u32 count, void (*visit)(char const*, Print&),
    Print& s) {
  print('[', s);
  if (count) {
    visit(base, s);
    while (--count) {
      print(", ", s);
      visit(base += size, s);
    }
  }
  print(']', s);
}
