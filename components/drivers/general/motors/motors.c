#define DEBUG_MODULE "MOTORS"
#include "debug_cf.h"
#include "motors.h"
#include "motors_brushless.h"
#include "motors_brushed.h"

#ifdef CONFIG_BRUSHLESS
    #define DEFAULT_MOTORS MotorsTypeBrushless
#endif
#ifdef CONFIG_BRUSHED
    #define DEFAULT_MOTORS MotorsTypeBrushed  
#endif
static MotorsType currentMotors = MotorsTypeAny;

static void initMotors();

typedef struct {
  void (*init)(void);
  bool (*test)(void);
  void (*applyAll)(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4);
  void (*applyChannel)(uint8_t channel, uint16_t ithrust);
  const char* name;
} MotorsFcns;

static MotorsFcns motorsFunctions[] = {
  {.init = 0, .test = 0, .applyAll = 0, .applyChannel = 0, .name = "None"}, // Any
  {.init = motorsBrushedInit, .test = motorsBrushedTest, .applyAll = motorsBrushedApplyAll, .applyChannel = motorsBrushlessApplyChannel, .name = "Brushed"},
  {.init = motorsBrushlessInit, .test = motorsBrushlessTest, .applyAll = motorsBrushlessApplyAll, .applyChannel = motorsBrushlessApplyChannel, .name = "Brushless"},
};

void motorsInit(MotorsType motors) {
  if (motors < 0 || motors >= MotorsType_COUNT) {
    return;
  }

  currentMotors = motors;

  if (MotorsTypeAny == currentMotors) {
    currentMotors = DEFAULT_MOTORS;
  }

  MotorsType forcedmotors = MotorsTypeAny;
  if (forcedmotors != MotorsTypeAny) {
    DEBUG_PRINTD("motors type forced\n");
    currentMotors = forcedmotors;
  }

  initMotors();

  DEBUG_PRINTD("Using %s (%d) motors\n", motorsGetName(), currentMotors);
}

MotorsType getMotorsType(void) {
  return currentMotors;
}

static void initMotors() {
  motorsFunctions[currentMotors].init();
}

const char* motorsGetName() {
  return motorsFunctions[currentMotors].name;
}


bool motorsTest(void) {
  return motorsFunctions[currentMotors].test();
}

void motorsApplyAll(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4) {
  motorsFunctions[currentMotors].applyAll(ithrust1, ithrust2, ithrust3, ithrust4);
}

void motorsApplyChannel(uint8_t channel, uint16_t ithrust) {
  motorsFunctions[currentMotors].applyChannel(channel, ithrust);
}