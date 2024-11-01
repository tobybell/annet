#pragma once

using usize = unsigned long long;
using isize = signed long;

#include <stdlib.h>
#include <string.h>

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned;
using u64 = unsigned long long ;
using i8 = signed char;
using i16 = signed short;
using i32 = signed;
using i64 = signed long long;

#define unimplemented abort()
#define unreachable   abort()

template <class T>
struct Type { using type = T; };

template <class T>
struct RemoveReference: Type<T> {};
template <class T>
struct RemoveReference<T&>: Type<T> {};
template <class T>
struct RemoveReference<T&&>: Type<T> {};
template <class T>
using remove_reference = typename RemoveReference<T>::type;

template <class T>
struct IsLvalueReference { enum { value = 0 }; };
template <class T>
struct IsLvalueReference<T&> { enum { value = 1 }; };
template <class T>
constexpr bool is_lvalue_reference = IsLvalueReference<T>::value;

template <class T>
constexpr remove_reference<T>&& move(T&& x) {
  return static_cast<remove_reference<T>&&>(x);
}

template <class T>
constexpr T&& forward(remove_reference<T>& x) {
  return static_cast<T&&>(x);
}

template <class T>
constexpr T&& forward(remove_reference<T>&& x) {
  static_assert(!is_lvalue_reference<T>);
  return static_cast<T&&>(x);
}

template <class T1, class T2>
constexpr T1 exchange(T1& x, T2&& new_value) {
  T1 old_value = ::move(x);
  x = ::forward<T2>(new_value);
  return old_value;
}

template <class T>
constexpr void swap(T& x, T& y) {
  T t(::move(x));
  x = ::move(y);
  y = ::move(t);
}

constexpr void check(bool condition) {
  if (!condition)
    abort();
}

template <class T>
constexpr bool is_trivial = __is_trivial(T);

template <class T, class... S>
constexpr bool is_trivially_constructible = __is_trivially_constructible(T, S...);

template <class T>
constexpr bool is_move_constructible = __is_constructible(T, T&&);

template <class T, class S>
constexpr bool is_trivially_assignable = __is_trivially_assignable(T, S);

template <class T>
constexpr bool is_trivially_destructible = __has_trivial_destructor(T);

template <class, class>
struct IsSame { enum { value = 0 }; };

template <class T>
struct IsSame<T, T> { enum { value = 1 }; };

template <class T, class S>
constexpr bool is_same = IsSame<T, S>::value;

template <bool>
struct EnableIf {};

template <>
struct EnableIf<true> { using type = int; };

template <bool Cond>
using enable_if = typename EnableIf<Cond>::type;

template <class T>
struct Decay { using type = T; };

template <class T>
struct Decay<T const&> { using type = T; };

template <class T>
struct Decay<T&&> { using type = T; };

template <class T>
struct Decay<T&> { using type = T; };

template <class T>
using decay = typename Decay<T>::type;

template <class... T>
struct Types {
  static constexpr u32 len = sizeof...(T);
};

template <class T, class S>
struct TypeIndex;
template <class T, class... R>
struct TypeIndex<Types<T, R...>, T> {
  static constexpr u32 value = 0u;
};
template <class T, class... R, class S>
struct TypeIndex<Types<T, R...>, S> {
  static constexpr u32 value = TypeIndex<Types<R...>, S>::value + 1;
};
template <class T, class S>
constexpr u32 type_index = TypeIndex<decay<T>, decay<S>>::value;

template <class T, class S>
struct TypeHas {
  static constexpr bool value = false;
};
template <class T, class... R>
struct TypeHas<Types<T, R...>, T> {
  static constexpr bool value = true;
};
template <class T, class... R, class S>
struct TypeHas<Types<T, R...>, S> {
  static constexpr bool value = TypeHas<Types<R...>, S>::value;
};
template <class T, class S>
constexpr bool type_has = TypeHas<decay<T>, decay<S>>::value;

template <class T>
T&& try_add_rvalue_reference(int);
template <class T>
T try_add_rvalue_reference(...);

template <class T>
decltype(try_add_rvalue_reference<T>(0)) declval() noexcept;

template <class F, class T, class... R>
struct RetTypeFirst {
  using type = decltype(declval<F>()(declval<T>()));
};

template <class T>
struct ObjOps {
  static void destruct(char* data) { reinterpret_cast<T*>(data)->~T(); }
  static void copy_construct(char* lhs, char const* rhs) {
    new (reinterpret_cast<T*>(lhs)) T(*reinterpret_cast<T const*>(rhs));
  }
  static void move_construct(char* lhs, char* rhs) {
    new (reinterpret_cast<T*>(lhs)) T(reinterpret_cast<T&&>(*rhs));
  }
  static bool compare_equal(char const* lhs, char const* rhs) {
    return *(T const*) lhs == *(T const*) rhs;
  }
  static void copy_assign(char* lhs, char const* rhs) {
    *(T*) lhs = *(T const*) rhs;
  }
};

template <class... S>
static constexpr usize max_of(S... args) {
  usize result = 0;
  ((args > result ? result = args : 0), ...);
  return result;
}

template <class... T>
struct Any {
  static_assert(sizeof...(T) < 256);

  static constexpr u32 len = sizeof...(T);

  using types = Types<T...>;

  alignas(max_of(alignof(T)...)) char data[max_of(sizeof(T)...)];
  u8 type {};

  static constexpr void (*destruct[len])(char*) {ObjOps<T>::destruct...};
  static constexpr void (*copy[len])(char*, char const*) {
      ObjOps<T>::copy_construct...};
  static constexpr void (*move[len])(char*, char*) {
      ObjOps<T>::move_construct...};
  static constexpr bool (*cmpeq[len])(char const*, char const*) {
      ObjOps<T>::compare_equal...};
  static constexpr void (*copy_assign[len])(char*, char const*) {
      ObjOps<T>::copy_assign...};

  Any() {
    using F = typename FirstType<T...>::type;
    new (data) F();
  }

  template <class S, enable_if<type_has<types, decay<S>>> = 0>
  Any(S&& arg): type(type_index<types, decay<S>>) {
    new (data) decay<S>(::forward<S>(arg));
  }

  Any(Any const& rhs): type(rhs.type) { copy[type](data, rhs.data); }

  Any(Any&& rhs): type(rhs.type) { move[type](data, rhs.data); }

  ~Any() { destruct[type](data); }

  template <class F>
  struct Visitors {
    using Ret = typename RetTypeFirst<F, T...>::type;
    Ret (*table[sizeof...(T)])(char const*, F&) {do_visit<T>...};

    template <class X>
    static Ret do_visit(char const* data, F& f) {
      return f(*reinterpret_cast<X const*>(data));
    }
  };

  template <class F>
  typename RetTypeFirst<F, T...>::type visit(F&& f) const {
    static constexpr Visitors<F> visitors;
    return visitors.table[type](data, f);
  }

  template <class S>
  static Any from(S&& arg) {
    return ::forward<S>(arg);
  }

  template <class S>
  bool is() const {
    return type == type_index<Types<T...>, S>;
  }

  template <class S>
  S& as() {
    check(type == type_index<Types<T...>, S>);
    return reinterpret_cast<S&>(data);
  }

  template <class S>
  S const& as() const {
    check(type == type_index<Types<T...>, S>);
    return reinterpret_cast<S const&>(data);
  }

  template <class S, enable_if<type_has<types, decay<S>>> = 0>
  decay<S>& operator=(S&& object) {
    using Sd = decay<S>;
    u8 new_type = type_index<Types<T...>, Sd>;
    if (new_type != type) {
      destruct[type](data);
      type = new_type;
      new (data) Sd(::forward<S>(object));
    } else {
      reinterpret_cast<Sd&>(data) = ::forward<S>(object);
    }
    return reinterpret_cast<Sd&>(data);
  }

  void operator=(Any const& rhs) {
    if (rhs.type != type) {
      destruct[type](data);
      type = rhs.type;
      copy[type](data, rhs.data);
    } else {
      copy_assign[type](data, rhs.data);
    }
  }

  bool operator==(Any const& rhs) const {
    return type == rhs.type && cmpeq[type](data, rhs.data);
  }

private:
  template <class F, class... R>
  struct FirstType {
    using type = F;
  };
};

template <class T>
struct AnyTypes;

template <class... T>
struct AnyTypes<Any<T...>> {
  using type = Types<T...>;
};

template <class T, class S>
constexpr u32 any_index = type_index<typename AnyTypes<T>::type, S>;

template <class T>
struct Span {
  T const* base;
  u32 size;
  constexpr Span() = default;
  constexpr Span(T const* base_, u32 size_): base(base_), size(size_) {}
  constexpr Span(T const* begin, T const* end): Span(begin, u32(end - begin)) {}
  T const* begin() const { return base; }
  T const* end() const { return base + size; }
  T const& operator[](u32 index) const { return base[index]; }
  friend u32 len(Span const& x) { return x.size; }
  explicit operator bool() const { return size; }
  template <class S>
  Span<S> reinterpret() const {
    static_assert(sizeof(S) == sizeof(T));
    return {reinterpret_cast<S const*>(base), size};
  }
  friend T const& last(Span const& span) {
    check(!!span);
    return span[len(span) - 1];
  }
};

using Str = Span<char>;

constexpr Str operator""_s(char const* s, usize size) {
  return {s, u32(size)};
}

constexpr Str to_str(char const* s) {
  u32 i {};
  while (s[i])
    ++i;
  return {s, i};
}

template <class T>
struct Mut {
  T* base;
  u32 size;
  constexpr Mut() = default;
  constexpr Mut(T* base_, u32 size_): base(base_), size(size_) {}
  template <u32 N>
  constexpr Mut(T (&arr)[N]): base(arr), size(N) {}
  T* begin() const { return base; }
  T* end() const { return base + size; }
  T& operator[](u32 index) const { return base[index]; }
  friend u32 len(Mut const& x) { return x.size; }
  explicit operator bool() const { return size; }
  Span<T> span() const { return {base, size}; }
  operator Span<T>() const { return {base, size}; }
};

template <class T>
struct Array {
  T* data {};
  u32 size {};

  Array() = default;
  Array(u32 size): size(size) {
    if constexpr (is_trivially_constructible<T>) {
      data = (T*) calloc(size, sizeof(T));
    } else {
      data = (T*) malloc(size * sizeof(T));
      for (u32 i {}; i < size; ++i)
        new (&data[i]) T;
    }
  }
  explicit Array(T* data_, u32 size_): data(data_), size(size_) {}
  Array(Span<T> span):
    data(reinterpret_cast<T*>(malloc(span.size * sizeof(T)))), size(span.size) {
    if constexpr (is_trivially_constructible<T, T const&>) {
      memcpy(data, span.base, size * sizeof(T));
    } else {
      for (u32 i {}; i < size; ++i)
        new (&data[i]) T(span[i]);
    }
  }
  Array(Mut<T> span): Array(span.span()) {}
  Array(Array const& rhs): Array(Span<T>(rhs)) {}
  Array(Array&& rhs):
    data(::exchange(rhs.data, nullptr)), size(::exchange(rhs.size, 0u)) {}
  ~Array() {
    if constexpr (!is_trivially_destructible<T>) {
      for (u32 i = size; i--;)
        data[i].~T();
    }
    free(data);
  }

  void operator=(Array rhs) {
    swap(data, rhs.data);
    size = ::exchange(rhs.size, 0u);
  }

  template <class S>
  Array<S>&& reinterpret() && {
    static_assert(sizeof(S) == sizeof(T));
    return reinterpret_cast<Array<S>&&>(*this);
  }

  T* begin() { return data; }
  T const* begin() const { return data; }
  T* end() { return begin() + size; }
  T const* end() const { return begin() + size; }
  explicit operator bool() const { return size; }
  friend u32 len(Array const& list) { return list.size; }
  T& operator[](u32 index) { return data[index]; }
  T const& operator[](u32 index) const { return data[index]; }
  Span<T> span() const { return {data, size}; }
  Mut<T> mut() { return {data, size}; }

  explicit operator bool() { return size; }
  operator Mut<T>() { return {data, size}; }
  operator Span<T>() const { return {data, size}; }

  friend T const& last(Array const& arr) {
    check(arr.size);
    return arr.data[arr.size - 1];
  }
};

template <class T>
constexpr bool is_trivially_copyable = __is_trivially_copyable(T);

template <class T>
struct List {
  static_assert(is_trivial<T>);
  T* data = nullptr;
  u32 size = 0;
  u32 capacity = 0;

  List() = default;

  List(Span<T> const& rhs): data(reinterpret_cast<T*>(malloc(rhs.size * sizeof(T)))), size(rhs.size), capacity(rhs.size) {
    memcpy(data, rhs.base, size * sizeof(T));
  }

  List(List const& rhs): List(rhs.span()) {}

  List(List&& rhs):
    data(::exchange(rhs.data, nullptr)), size(::exchange(rhs.size, 0u)),
    capacity(::exchange(rhs.capacity, 0u)) {}

  List(Array<T>&& rhs):
    data(::exchange(rhs.data, nullptr)), size(::exchange(rhs.size, 0u)),
    capacity(size) {}

  ~List() {
    if constexpr (!is_trivially_destructible<T>) {
      for (u32 i = 0; i < size; ++i)
        data[i].~T();
    }
    free(data);
  }

  void operator=(List rhs) {
    swap(data, rhs.data);
    swap(size, rhs.size);
    swap(capacity, rhs.capacity);
  }

  T& emplace() {
    expand(size + 1);
    u32 i = size++;
    return data[i] = {};
  }

  T& push(T const& value) {
    expand(size + 1);
    u32 i = size++;
    return data[i] = value;
  }

  T& push(T&& value) {
    expand(size + 1);
    u32 i = size++;
    if constexpr (is_trivially_assignable<T, T&&>) {
      return data[i] = ::move(value);
    } else {
      return *(new (&data[i]) T(::move(value)));
    }
  }

  void pop() {
    --size;
    if constexpr (!is_trivially_destructible<T>)
      data[size].~T();
  }

  T const& last() const {
    check(size);
    return data[size - 1];
  }

  void expand(u32 needed) {
    if (needed <= capacity)
      return;
    if (!capacity)
      capacity = needed;
    else
      while (capacity < needed)
        capacity *= 2;
    
    if constexpr (is_trivially_constructible<T, T const&>) {
      data = reinterpret_cast<T*>(realloc(data, capacity * sizeof(T)));
    } else {
      T* new_data = reinterpret_cast<T*>(malloc(capacity * sizeof(T)));
      for (u32 i {}; i < size; ++i) {
        new (&new_data[i]) T(::move(data[i]));
        data[i].~T();
      }
      free(::exchange(data, new_data));
    }
  }

  void resize(u32 new_size) {
    capacity = new_size;
    size = new_size;
    data = reinterpret_cast<T*>(realloc(data, new_size * sizeof(T)));
  }

  T* begin() { return data; }
  T const* begin() const { return data; }
  T* end() { return begin() + size; }
  T const* end() const { return begin() + size; }
  friend u32 len(List const& list) { return list.size; }
  T& operator[](u32 index) { return data[index]; }
  T const& operator[](u32 index) const { return data[index]; }
  explicit operator bool() const { return size; }

  Array<T> take() {
    capacity = 0;
    return Array<T>(::exchange(data, nullptr), ::exchange(size, 0u));
  }

  Span<T> span() const { return {data, size}; }
  operator Span<T>() const { return span(); }

  friend T const& last(List const& list) {
    check(!!list);
    return list[len(list) - 1];
  }
};

using String = Array<char>;

struct RangePtr {
  u32 value;
  u32 operator*() const { return value; }
  void operator++() { ++value; }
  bool operator!=(RangePtr const& rhs) const { return value != rhs.value; }
};

struct Range {
  u32 start;
  u32 size;
  RangePtr begin() { return {start}; }
  RangePtr end() { return {start + size}; }
};

inline Range range(u32 size) { return {0, size}; }

inline Range range(u32 start, u32 stop) {
  check(start <= stop);
  return {start, stop - start};
}

template <class T>
void extend(List<T>& lhs, Span<T> rhs) {
  u32 old_len = len(lhs);
  u32 new_stuff = len(rhs);
  u32 new_size = old_len + new_stuff;
  lhs.expand(new_size);
  memcpy(&lhs[old_len], rhs.base, new_stuff * sizeof(T));
  lhs.size = new_size;
}

template <class T>
constexpr void copy(T* dst, Span<T> src) {
  for (u32 i {}; i < src.size; ++i)
    new (dst + i) T(src[i]);
}

template <class T>
struct ArrayList {
  List<T> list;
  List<u32> ofs;

  ArrayList() { ofs.push(0); }
  ArrayList(List<T> list_, List<u32> ofs_):
    list(::move(list_)), ofs(::move(ofs_)) {}

  Mut<T> push_empty(u32 size) {
    auto orig_size = list.size;
    list.expand(list.size + size);
    list.size += size;
    ofs.push(list.size);
    return {&list[orig_size], size};
  }

  Mut<T> push(Span<T> items) {
    auto storage = push_empty(len(items));
    copy(storage.begin(), items);
    return storage;
  }

  void pop() {
    ofs.pop();
    list.size = ofs.last();
  }

  friend u32 len(ArrayList const& list) { return list.ofs.size - 1; }
  Mut<T> operator[](u32 index) {
    return {&list[ofs[index]], ofs[index + 1] - ofs[index]};
  }
  Span<T> operator[](u32 index) const {
    return {&list[ofs[index]], ofs[index + 1] - ofs[index]};
  }
  explicit operator bool() const { return len(ofs) > 1; }

  friend Span<T> last(ArrayList const& list) {
    check(!!list);
    return list[len(list) - 1];
  }
};

constexpr struct None {
} none;

template <class T>
struct Maybe {
  alignas(alignof(T)) char data[sizeof(T)];
  bool exists {};
  Maybe() = default;
  Maybe(None): Maybe() {}

  Maybe(T const& value): exists(true) { new (ptr()) T(value); }
  Maybe(T&& value): exists(true) { new (ptr()) T(::move(value)); }
  Maybe(Maybe const& rhs): exists(rhs.exists) {
    if (rhs.exists)
      new (ptr()) T(*rhs);
  }
  Maybe(Maybe&& rhs): exists(rhs.exists) {
    if (exists)
      new (ptr()) T(::move(*rhs));
  }

  ~Maybe() {
    if (exists)
      ptr()->~T();
  }

  T* ptr() { return reinterpret_cast<T*>(data); }
  T const* ptr() const { return reinterpret_cast<T const*>(data); }

  T& operator*() { return *operator->(); }
  T const& operator*() const { return *operator->(); }
  T* operator->() {
    check(exists);
    return ptr();
  }
  T const* operator->() const {
    check(exists);
    return ptr();
  }
  void operator=(None const&) { exists = false; }
  void operator=(T const& new_value) {
    if (exists)
      *ptr() = new_value;
    else {
      new (ptr()) T(new_value);
      exists = true;
    }
  }
  void operator=(Maybe const& rhs) {
    if (exists && rhs.exists)
      *ptr() = *rhs.ptr();
    else if (exists && !rhs.exists) {
      ptr()->~T();
      exists = false;
    } else if (rhs.exists) {
      new (ptr()) T(*rhs.ptr());
      exists = true;
    }
  }
  explicit operator bool() const { return exists; }
};

template <class T>
constexpr Maybe<decay<T>> some(T&& x) {
  return ::forward<T>(x);
}

struct MaybeU32 {
  u32 val {};
  MaybeU32() = default;
  constexpr MaybeU32(None) {}
  constexpr MaybeU32(u32 v): val(v + 1) {}
  explicit operator bool() const { return val; }
  u32 operator*() const {
    check(val);
    return val - 1;
  }
  bool operator==(MaybeU32 const& rhs) const { return val == rhs.val; }
};

template <class T, class S>
MaybeU32 find(Span<T> const& span, S const& elem) {
  for (auto i: range(len(span)))
    if (span[i] == elem)
      return i;
  return {};
}

template <class T>
MaybeU32 find(Array<T> const& arr, T const& elem) {
  return find(arr.span(), elem);
}

template <class T, class F>
MaybeU32 find_if(Span<T> const& span, F&& pred) {
  for (auto i: range(len(span)))
    if (pred(span[i]))
      return i;
  return {};
}

template <class T>
void pop_n(List<T>& list, u32 n) {
  list.resize(len(list) - n);
}

template <class T>
Maybe<u32> find_reverse(Span<T> span, T const& val) {
  for (u32 i = len(span); i--;) {
    if (span[i] == val)
      return some(i);
  }
  return {};
}

template <class T>
struct ArrayArray {
  Array<T> items;
  Array<u32> offsets;

  friend u32 len(ArrayArray const& array) { return len(array.offsets) - 1; }
  Mut<T> operator[](u32 index) {
    return {&items[offsets[index]], offsets[index + 1] - offsets[index]};
  }
  Span<T> operator[](u32 index) const {
    return {&items[offsets[index]], offsets[index + 1] - offsets[index]};
  }
};

template <class T>
struct Own {
  Own() = default;
  Own(T* ptr_): ptr(ptr_) {}
  Own(Own const&) = delete;
  template <class S>
  Own(Own<S>&& rhs): ptr(rhs.release()) {}
  ~Own() {
    if (ptr)
      ptr->~T();
    free(::exchange(ptr, nullptr));
  }
  void operator=(Own rhs) { swap(ptr, rhs.ptr); }
  T* operator->() const { return ptr; }
  T& operator*() const { return *ptr; }
  T* release() { return ::exchange(ptr, nullptr); }

private:
  T* ptr {};
};

#define dump(x) println(#x, ": ", x)

template <class T, class... S>
using ReturnType = decltype(declval<T>()(declval<S>()...));

template <class T>
struct Void {
  using type = void;
};

template <class R, class T, class... S>
struct IsInvocable {
  enum { value = 0 };
};

template <class T, class... S>
struct IsInvocable<typename Void<ReturnType<T, S...>>::type, T, S...> {
  enum { value = 1 };
};

template <class T, class... S>
constexpr bool is_invocable = IsInvocable<void, T, S...>::value;

template <class... T>
struct Func {
  void* obj;
  void (*call)(T..., void*);
  template <
      class F,
      enable_if<
          is_trivially_copyable<F> && sizeof(F) <= sizeof(void*) &&
          is_invocable<F, T...>> = 0>
  Func(F const& f):
    call([](T... arg, void* x) {
      return (*reinterpret_cast<F*>(&x))(arg...);
    }) {
    memcpy(&obj, &f, sizeof(F));
  }
  void operator()(T... arg) const {
    return call(::forward<T>(arg)..., obj);
  }
};

template <class T>
struct ObjectMaybe {
  alignas(T) char value[sizeof(T)];
  bool exists {};

  ObjectMaybe() {}
  ObjectMaybe(T&& val): exists(true) { new (operator->()) T(::move(val)); }
  ObjectMaybe(T const& val) { operator=(val); }
  ~ObjectMaybe() {
    if (exists)
      operator->()->~T();
  }

  T& operator*() { return *operator->(); }
  T const& operator*() const { return *operator->(); }
  T* operator->() {
    check(exists);
    return (T*) value;
  }
  T const* operator->() const {
    check(exists);
    return (T const*) value;
  }
  void operator=(None const&) {
    if (exists) {
      operator->()->~T();
      exists = false;
    }
  }
  void operator=(T const& new_value) {
    if (exists)
      *operator->() = new_value;
    else {
      exists = true;
      new (operator->()) T(new_value);
    }
  }
  void operator=(T&& new_value) {
    if (exists)
      *operator->() = ::move(new_value);
    else {
      exists = true;
      new (operator->()) T(::move(new_value));
    }
  }
  explicit operator bool() const { return exists; }
};

template <u32... I>
struct Indices {};

template <u32 First, u32... I>
struct IndicesBuilder:
  IndicesBuilder<First - 1, First, I...> {};

template <u32... I>
struct IndicesBuilder<0u, I...> {
  using type = Indices<0u, I...>;
};

template <u32 Size>
struct MakeIndices {
  using type = typename IndicesBuilder<Size - 1>::type;
};

template <>
struct MakeIndices<0u> {
  using type = Indices<>;
};

template <u32 Size>
static constexpr typename MakeIndices<Size>::type indices {};
