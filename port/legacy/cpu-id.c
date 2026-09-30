/* Identify the execution context; no MSR, I/O port or device access. */
#include <cpuid.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned int eax, ebx, ecx, edx;
    char vendor[13] = {0};
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) return 1;
    memcpy(vendor, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
    printf("{\"vendor\":\"%s\",\"leaf1_eax\":\"%08x\"}\n", vendor, eax);
    return 0;
}
