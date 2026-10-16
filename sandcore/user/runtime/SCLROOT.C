/* 同一组普通C源既供GCC引导，也供SCCC在系统内生成根；没有main或EXIT。 */
#define SCL_BOOT_LIBRARY 1
#include "scl_format.c"
#include "scd_format.c"
#include "scldr.c"
#include "scl_platform.c"
#include "scl_debug_registry.c"
#include "scl_termination.c"
#include "scl_application.c"
