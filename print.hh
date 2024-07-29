#pragma once

struct Print {
  List<char> chars;
};

void print(u8 x, Print&);
void print(u16 x, Print&);
void print(u32 x, Print&);
void print(u64 x, Print&);
inline void print(unsigned long x, Print& p) { print(u64(x), p); }
void print(i8 x, Print&);
void print(i16 x, Print&);
void print(i32 x, Print&);
void print(i64 x, Print&);
inline void print(isize x, Print& p) { print(i64(x), p); }
void print(void* x, Print&);
inline void print(char x, Print& p) { p.chars.push(x); }
inline void print(char const* x, Print& p) { extend(p.chars, to_str(x)); }
inline void print(Str x, Print& p) { extend(p.chars, x); }
inline void print(bool x, Print& p) { print(x ? "true" : "false", p); }
template <class T, enable_if<is_invocable<T, Print&>> = 0>
inline void print(T&& x, Print& s) {
  x(s);
}

template <class... T>
void sprint(Print& p, T&&... x) {
  (print(x, p), ...);
}

void write_cerr(Str);

template <class... T>
void println(T&&... x) {
  Print p;
  sprint(p, ::forward<T>(x)..., '\n');
  write_cerr(p.chars.span());
}

void print_array(
    char const* base, u32 size, u32 count, void (*)(char const*, Print&),
    Print&);

template <class T>
inline void print(Span<T> arr, Print& s_) {
  using TC = T const;
  print_array(
      reinterpret_cast<char const*>(arr.begin()), sizeof(T), len(arr),
      [](char const* x, Print& s) { print(*(TC*) x, s); }, s_);
}
