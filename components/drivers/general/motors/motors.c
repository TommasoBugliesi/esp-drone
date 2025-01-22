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
  void (*apply)(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4);
  const char* name;
} MotorsFcns;

static MotorsFcns motorsFunctions[] = {
  {.init = 0, .test = 0, .apply = 0, .name = "None"}, // Any
  {.init = motorsBrushedInit, .test = motorsBrushedTest, .apply = motorsBrushedApply, .name = "Brushed"},
  {.init = motorsBrushlessInit, .test = motorsBrushlessTest, .apply = motorsBrushlessApply, .name = "Brushless"},
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

void motorsApply(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4) {
  motorsFunctions[currentMotors].apply(ithrust1, ithrust2, ithrust3, ithrust4);
}
