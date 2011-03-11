#include <os/memory.h>

const char *
process_get_account_name(process_account_num_t account_num)
{
    switch (account_num) {
    case PROCESS_ACCOUNT_DEFAULT:           return "default"; break;
    case PROCESS_ACCOUNT_EMERGENCY_GC:      return "emergency_gc"; break;
    case PROCESS_ACCOUNT_HEAP:              return "heap"; break;
    case PROCESS_ACCOUNT_PAUSE_PREVENTION:  return "pause_prevention"; break;
    default: break;
    }

    return "unknown";
}
