/*
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * system.c - Top level module implementation
 */

#include <stdbool.h>
#include <inttypes.h>
/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "version.h"
#include "config.h"
#include "param.h"
#include "log.h"
#include "led.h"
#include "ledseq.h"
#include "adc_esp32.h"
#include "pm_esplane.h"
#include "config.h"
#include "system.h"
#include "platform.h"
#include "worker.h"
#include "freeRTOSdebug.h"
#include "wifi_esp32.h"
#include "comm.h"
#include "stabilizer.h"
#include "commander.h"
#include "console.h"
#include "wifilink.h"
#include "espnow_link.h"
#include "mem.h"
#include "queuemonitor.h"
#include "buzzer.h"
#include "sound.h"
#include "sysload.h"
#include "estimator_kalman.h"
#include "app.h"
#include "stm32_legacy.h"
#define DEBUG_MODULE "SYS"
#include "debug_cf.h"
#include "static_mem.h"
#include "cfassert.h"

#ifndef START_DISARMED
  #define ARM_INIT true
#else
  #define ARM_INIT false
#endif

/* Private variable */
static bool selftestPassed;
static bool canFly;
static bool armed = ARM_INIT;
static bool forceArm;
static bool isInit;

STATIC_MEM_TASK_ALLOC(systemTask, SYSTEM_TASK_STACKSIZE);

/* System wide synchronisation */
xSemaphoreHandle canStartMutex;
static StaticSemaphore_t canStartMutexBuffer;

/* Private functions */
static void systemTask(void *arg);

/* Public functions */
void systemLaunch(void)
{
  STATIC_MEM_TASK_CREATE(systemTask, systemTask, SYSTEM_TASK_NAME, NULL, SYSTEM_TASK_PRI);
}

/* Modules in the order they are initialised.
 *
 * - A module without init is only tested, one without test is only initialised.
 * - Modules that are covered by the test of another one (wifilink, crtp,
 *   console and the log/param services are tested by commTest) have no test.
 * - If the test of an essential module fails the system is unusable, any other
 *   failure can be overridden by setting system.selftestPassed from the client.
 * - settleMs is a pause after the init.
 * - crtp and console come early so that DEBUG_PRINT can be used from here on.
 * - The Kalman estimator task is optional: add estimatorKalmanTaskInit() and
 *   estimatorKalmanTaskTest() after "commander" to enable it.
 */
typedef struct {
  const char *name;
  void (*init)(void);
  bool (*test)(void);
  bool essential;
  uint16_t settleMs;
} systemModule_t;

static void systemCoreInit(void);
static void ledStartupInit(void);
static void stabilizerStartupInit(void);

static const systemModule_t systemModules[] = {
  { "led",        ledStartupInit,         NULL,               false, 0 },
  { "wifi",       wifiInit,               wifiTest,           false, 0 },
  { "espnow",     espnowLinkInit,         espnowLinkTest,     false, 500 },
#ifdef DEBUG_QUEUE_MONITOR
  { "queuemon",   queueMonitorInit,       NULL,               false, 0 },
#endif
  { "system",     systemCoreInit,         NULL,               false, 0 },
  { "wifilink",   wifilinkInit,           NULL,               false, 0 },
  { "sysload",    sysLoadInit,            NULL,               false, 0 },
  { "crtp",       crtpInit,               NULL,               false, 0 },
  { "console",    consoleInit,            NULL,               false, 0 },
  { "worker",     workerInit,             workerTest,         true,  0 },
  { "ledseq",     ledseqInit,             ledseqTest,         true,  0 },
#ifdef CONFIG_ADC_ENABLE
  // Power management needs the ADC
  { "adc",        adcInit,                NULL,               false, 0 },
  { "pm",         pmInit,                 pmTest,             true,  0 },
#endif
#ifdef CONFIG_BUZZER_ENABLE
  { "buzzer",     buzzerInit,             buzzerTest,         true,  0 },
#endif
#ifdef APP_ENABLED
  { "app",        appInit,                NULL,               false, 0 },
#endif
  { "comm",       commInit,               commTest,           false, 0 },
  { "commander",  commanderInit,          commanderTest,      false, 0 },
  { "stabilizer", stabilizerStartupInit,  stabilizerTest,     false, 0 },
  { "sound",      soundInit,              soundTest,          false, 0 },
  { "mem",        memInit,                NULL,               false, 0 },
  { "assert",     NULL,                   cfAssertNormalStartTest, false, 0 },
};

#define SYSTEM_MODULE_COUNT (sizeof(systemModules) / sizeof(systemModules[0]))

static void ledStartupInit(void)
{
  ledInit();
  ledSet(CHG_LED, 1);   // stays on until the system has started
}

static void stabilizerStartupInit(void)
{
  stabilizerInit(anyEstimator);
}

/* Creates the lock the other tasks wait on in systemWaitStart(). It is held
 * until systemStart(). */
static void systemCoreInit(void)
{
  if (isInit) {
    return;
  }

  canStartMutex = xSemaphoreCreateMutexStatic(&canStartMutexBuffer);
  xSemaphoreTake(canStartMutex, portMAX_DELAY);

  isInit = true;
}

static void systemInitModules(void)
{
  for (size_t i = 0; i < SYSTEM_MODULE_COUNT; i++) {
    const systemModule_t *module = &systemModules[i];

    if (module->init) {
      module->init();
    }
    if (module->settleMs) {
      vTaskDelay(M2T(module->settleMs));
    }
  }
}

/* Runs the test of every module and reports each failure by name.
 * Returns true if all passed, essentialOk tells if the essential ones did. */
static bool systemTestModules(bool *essentialOk)
{
  bool pass = isInit;

  *essentialOk = isInit;

  for (size_t i = 0; i < SYSTEM_MODULE_COUNT; i++) {
    const systemModule_t *module = &systemModules[i];

    if (module->test && !module->test()) {
      DEBUG_PRINTE("Self-test of %s failed", module->name);
      pass = false;
      if (module->essential) {
        *essentialOk = false;
      }
    }
  }

  return pass;
}

/* Private functions implementation */

void systemTask(void *arg)
{
  bool essentialOk;

  systemInitModules();

  if (systemTestModules(&essentialOk))
  {
    selftestPassed = 1;
    systemStart();
    DEBUG_PRINTI("systemStart ! selftestPassed = %d", selftestPassed);
    soundSetEffect(SND_STARTUP);
    ledseqRun(&seq_alive);
    ledseqRun(&seq_testPassed);
  }
  else
  {
    selftestPassed = 0;
    if (essentialOk)
    {
      while(1)
      {
        ledseqRun(&seq_testFailed);
        vTaskDelay(M2T(2000));
        // System can be forced to start by setting the param to 1 from the cfclient
        if (selftestPassed)
        {
          DEBUG_PRINT("Start forced.\n");
          systemStart();
          break;
        }
      }
    }
    else
    {
      ledSet(SYS_LED, true);
    }
  }
  DEBUG_PRINT("Free heap: %"PRIu32" bytes\n", xPortGetFreeHeapSize());

  workerLoop();

  //Should never reach this point!
  while(1)
    vTaskDelay(portMAX_DELAY);

}

/* Global system variables */
void systemStart()
{
  xSemaphoreGive(canStartMutex);
#ifndef DEBUG_EP2
  //watchdogInit();
#endif
}

void systemWaitStart(void)
{
  //This permits to guarantee that the system task is initialized before other
  //tasks waits for the start event.
  while(!isInit)
    vTaskDelay(2);

  xSemaphoreTake(canStartMutex, portMAX_DELAY);
  xSemaphoreGive(canStartMutex);
}

void systemSetCanFly(bool val)
{
  canFly = val;
}

bool systemCanFly(void)
{
  return canFly;
}

void systemSetArmed(bool val)
{
  armed = val;
}

bool systemIsArmed()
{

  return armed || forceArm;
}
// void vApplicationIdleHook( void )
// {
//   static uint32_t tickOfLatestWatchdogReset = M2T(0);

//   portTickType tickCount = xTaskGetTickCount();

//   if (tickCount - tickOfLatestWatchdogReset > M2T(WATCHDOG_RESET_PERIOD_MS))
//   {
//     tickOfLatestWatchdogReset = tickCount;
//     watchdogReset();
//   }

//   // Enter sleep mode. Does not work when debugging chip with SWD.
//   // Currently saves about 20mA STM32F405 current consumption (~30%).
// #ifndef DEBUG
//   { __asm volatile ("wfi"); }
// #endif
// }

/*System parameters (mostly for test, should be removed from here) */
/*PARAM_GROUP_START(cpu)
PARAM_ADD(PARAM_UINT16 | PARAM_RONLY, flash, MCU_FLASH_SIZE_ADDRESS)
PARAM_ADD(PARAM_UINT32 | PARAM_RONLY, id0, MCU_ID_ADDRESS+0)
PARAM_ADD(PARAM_UINT32 | PARAM_RONLY, id1, MCU_ID_ADDRESS+4)
PARAM_ADD(PARAM_UINT32 | PARAM_RONLY, id2, MCU_ID_ADDRESS+8)
PARAM_GROUP_STOP(cpu)*/

PARAM_GROUP_START(system)
PARAM_ADD(PARAM_INT8 | PARAM_RONLY, selftestPassed, &selftestPassed)
PARAM_ADD(PARAM_INT8, forceArm, &forceArm)
PARAM_GROUP_STOP(sytem)

/* Loggable variables */
LOG_GROUP_START(sys)
LOG_ADD(LOG_INT8, canFly, &canFly)
LOG_ADD(LOG_INT8, armed, &armed)
LOG_GROUP_STOP(sys)
