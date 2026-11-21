/* Sample program linked against mathlib (#14): exits 0 when the library works. */
#include <stdio.h>

#include "mathlib.h"

int main(void) {
    const int sum = add_numbers(2, 3);
    printf("2 + 3 = %d\n", sum);
    return sum == 5 ? 0 : 1;
}
