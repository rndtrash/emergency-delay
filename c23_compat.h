//
// Created by ivan on 08.09.23.
//

#ifndef EMERGENCY_DELAY_C23_COMPAT_H
#define EMERGENCY_DELAY_C23_COMPAT_H

/* nullptr
 * https://stackoverflow.com/a/76900130
 * https://en.cppreference.com/w/c/compiler_support/23
 */

/* C++11 or later? */
#if (defined(__cplusplus) && __cplusplus >= 201103L)

#include <cstddef>

/* C23 or later? If GCC, is it version 13 or newer? */
#elif (\
        defined(__STDC__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L) || \
        defined(__GNUC__) && __GNUC__ >= 13 \
)

#include <stddef.h> /* nullptr_t */

/* pre C23, pre C++11 or non-standard */
#else

#define nullptr (void*)0
typedef void *nullptr_t;

#endif

/* nodiscard
 * https://en.cppreference.com/w/c/compiler_support/23
 */

/* C++17 or later? C23 or later? If GCC, is it version 10 or newer? If clang, is it version 9 or newer? */
#if (\
        defined(__cplusplus) && __cplusplus >= 201703L || \
        defined(__STDC__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L) || \
        defined(__GNUC__) && __GNUC__ >= 10 || \
        defined(__clang_major__) && __clang_major__ >= 9\
)

#define NODISCARD [[nodiscard]]

/* Has __warn_unused_result__ ? */
#elif __has_attribute(warn_unused_result)

#define NODISCARD __attribute__((__warn_unused_result__))

/* pre C23, pre C++11 or non-standard */
#else

#warning "Your compiler does not have a [[nodiscard]] support. If it actually supports this attribute, please modify the c23_compat header."
#define NODISCARD

#endif

#endif //EMERGENCY_DELAY_C23_COMPAT_H
