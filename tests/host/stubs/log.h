#define LOG_UINT8 1
#define LOG_UINT32 2
#define LOG_GROUP_START(n)
#define LOG_ADD(t,n,a)
#define LOG_GROUP_STOP(n)
#include <stdint.h>
typedef uint16_t logVarId_t;
#define LOG_VARID_IS_VALID(varId) (varId != 0xffffu)
logVarId_t logGetVarId(char *group, char *name);
float logGetFloat(logVarId_t varid);
unsigned int logGetUint(logVarId_t varid);
