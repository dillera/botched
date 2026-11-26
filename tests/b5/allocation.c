/* Link-time allocation tripwires. Codec and mirror may never allocate. */
#include <stdlib.h>
void *__wrap_malloc(size_t n) { (void)n; abort(); }
void *__wrap_calloc(size_t n, size_t s) { (void)n; (void)s; abort(); }
void *__wrap_realloc(void *p, size_t n) { (void)p; (void)n; abort(); }
void __wrap_free(void *p) { (void)p; abort(); }
