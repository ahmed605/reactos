#include "pch.h"

/* Headers that shouldn't be precompiled due to GCC bugs */
/* Probe and capture */
#include <reactos/probe.h>

/* Job UI Restriction Constants */
#define JOB_OBJECT_UILIMIT_HANDLES          0x0001
#define JOB_OBJECT_UILIMIT_READCLIPBOARD    0x0002
#define JOB_OBJECT_UILIMIT_WRITECLIPBOARD   0x0004
#define JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS 0x0008
#define JOB_OBJECT_UILIMIT_DISPLAYSETTINGS  0x0010
#define JOB_OBJECT_UILIMIT_GLOBALATOMS      0x0020
#define JOB_OBJECT_UILIMIT_DESKTOP          0x0040
#define JOB_OBJECT_UILIMIT_EXITWINDOWS      0x0080

#ifdef __cplusplus
#undef NULL
#define NULL nullptr
#endif
