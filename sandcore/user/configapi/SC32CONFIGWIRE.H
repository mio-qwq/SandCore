#ifndef SANDCORE_SC32CONFIGWIRE_PRIVATE_H
#define SANDCORE_SC32CONFIGWIRE_PRIVATE_H
#include "../SC32CONFIG.H"
enum {SC32_C_CONFIG_APPLY=0x007,SC32_C_ERROR_OPERATION=0x1400};
typedef struct {
    ScU32 size,version,kind,action,candidate,flags,expected_candidate_revision,expected_target_revision,reserved[8];
} Sc32ConfigurationRequest;
#endif
