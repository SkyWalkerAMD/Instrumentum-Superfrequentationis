/* Compile-only Kbuild capability probe; not part of the installed module. */
#include <linux/module.h>
#include <linux/device.h>

static inline struct class *octool_class_probe(void)
{
#ifdef PK_CLASS_CREATE_PROBE_ONE_ARG
    return class_create("octool-probe");
#else
    return class_create(THIS_MODULE, "octool-probe");
#endif
}
