#include <rawscan_static.h>
#include <../tests/rawscan_test.c>

/*
 * The rawscan_static_test module is the same as the rawscan_test
 * module, except that it is built using rawscan_static.h, which
 * directly includes the main rawscan code in the application
 * compilation unit, rather than linking to the librawscan.so
 * shared library.
 */
