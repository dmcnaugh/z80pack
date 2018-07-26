#include <pthread.h>

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
// int pthread_attr_setdetachstate(pthread_attr_t *, int);

int pthread_attr_init(pthread_attr_t *attr) {

    (void)attr;
    attr = NULL;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {

    (void)attr;
    attr = NULL;
    return 0;
}

// int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {

//     (void)attr;
//     (void)detachstate;
//     attr = NULL;
//     return 0;
// }