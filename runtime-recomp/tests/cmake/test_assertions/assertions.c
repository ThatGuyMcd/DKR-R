#include <assert.h>

#ifdef NDEBUG
#error Test assertions must remain enabled in optimized configurations.
#endif

int main(void) {
    int calls = 0;
    assert(++calls == 1);
    return calls == 1 ? 0 : 1;
}
