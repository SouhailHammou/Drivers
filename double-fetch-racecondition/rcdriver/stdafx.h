#ifndef _WIN32_WINNT                
#define _WIN32_WINNT 0x0501
#endif						

#ifdef __cplusplus
extern "C" 
{

#endif

#include <ntifs.h>
#include <ntddk.h>
#include <time.h>
#include <stdlib.h>
#define IOCTL_RaceCondition CTL_CODE(FILE_DEVICE_UNKNOWN,0x815, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#ifdef __cplusplus
}
#endif
