// qt5_msvc_compat.h — подключается ко всем файлам проекта (/FI) до заголовков Qt.
//
// Заголовки Qt 5.12 на MSVC используют stdext::make_checked_array_iterator и
// stdext::make_unchecked_array_iterator. В STL из VS 2022 они устарели, а в MSVC 14.50+
// (VS 18) удалены — Qt перестаёт собираться. Здесь — замена: обычный указатель
// (ровно так Qt поступает на всех остальных компиляторах).
#pragma once

#if defined(_MSC_VER) && _MSC_VER >= 1950 && defined(__cplusplus)
#include <cstddef>

namespace stdext {
    template<class T>
    inline T* make_checked_array_iterator(T* ptr, std::size_t) { return ptr; }

    template<class T>
    inline T* make_unchecked_array_iterator(T* ptr) { return ptr; }
}
#endif
