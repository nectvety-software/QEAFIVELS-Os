#ifndef VQEAF_VERSION_H
#define VQEAF_VERSION_H

/* Single source of truth for the OS identity. Every user-visible surface
 * (boot splash, recovery screen, USB descriptors, DEVINFO.TXT, build log)
 * builds its name from these macros so there is exactly one OS version:
 * VQEAF OS. */
#define VQEAF_OS_NAME    "VQEAF OS"
#define VQEAF_OS_VERSION "0.2.0"
#define VQEAF_OS_STRING  VQEAF_OS_NAME " " VQEAF_OS_VERSION

/* Build metadata — set by the build script or CI. */
#ifndef VQEAF_BUILD_DATE
#define VQEAF_BUILD_DATE __DATE__
#endif
#ifndef VQEAF_BUILD_TARGET
#define VQEAF_BUILD_TARGET "x86_64-uefi"
#endif

#endif /* VQEAF_VERSION_H */
