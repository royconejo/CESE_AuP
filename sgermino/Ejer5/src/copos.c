/* Copyright 2013, Michael J. Pont.
 * Copyright 2016, Eric Pernia.
 * Copyright 2018, Santiago Germino.
 * All rights reserved.
 *
 * This file is part sAPI library for microcontrollers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "copos.h"
#include "systick.h"
#include "chip.h"   // CMSIS
#include <string.h>


// Keeps track of time since last error was recorded (see below)
//static int32_t errorTickCount;

// The code of the last error (reset after ~1 minute)
//static char lastErrorCode;


// The array of tasks
sTask_t schedulerTasks[SCHEDULER_MAX_TASKS];

// Used to display the error code
char errorCode = 0;


/*
    Scheduler initialization function. Prepares scheduler data structures.
    Must call this function before using the scheduler.
*/
void schedulerInit (void)
{
    for (uint32_t i = 0; i < SCHEDULER_MAX_TASKS; ++i)
    {
        schedulerDeleteTask (i);
    }

   /* Reset the global error variable
      - schedulerDelete_Task() will generate an error code,
        (because the task array is empty) */
    errorCode = 0;
}


/*
    This is the scheduler ISR. It is called at a rate determined by the timer
    settings in the 'init' function.
*/
void schedulerUpdate (uint32_t ticks)
{
    // NOTE: calculations are in *TICKS* (not milliseconds)
    for (uint32_t i = 0; i < SCHEDULER_MAX_TASKS; ++i)
    {
        sTask_t* t = &schedulerTasks[i];
        // Check if there is a task at this location
        if (!t->pTask || -- t->delay > 0)
        {
            continue;
        }

        // The task is due to run
        ++ t->runMe;
        if (t->period)
        {
            // Schedule regular tasks to run again
            t->delay = t->period;
        }
    }
}

/*
    Starts the scheduler, by enabling timer interrupts.
    NOTE:   Usually called after all regular tasks are added, to keep the tasks
            synchronized.
    NOTE:   ONLY THE SCHEDULER INTERRUPT SHOULD BE ENABLED!!!
*/
void schedulerStart (uint32_t tickRateMs)
{
    SYSTICK_SetMillisecondPeriod (tickRateMs);
    SYSTICK_SetHook (schedulerUpdate);
}


/*
    This is the 'dispatcher' function. When a task (function)
    is due to run, schedulerDispatchTasks() will run it.
    This function must be called (repeatedly) from the main loop.
*/
void schedulerDispatchTasks (uint32_t ticks)
{
    // Dispatches (runs) the next task (if one is ready)
    for (uint32_t i = 0; i < SCHEDULER_MAX_TASKS; ++i)
    {
        sTask_t* t = &schedulerTasks[i];

        if (t->runMe > 0)
        {
            // Run the task
            t->pTask (t->context, ticks);
            -- t->runMe;

            // Periodic tasks will automatically run again
            // - if this is a 'one shot' task, remove it from the array
            if (t->period == 0)
            {
                schedulerDeleteTask (i);
            }
        }
    }

    schedulerReportStatus ();
    __WFI ();
}


/*
    Causes a task (function) to be executed at regular intervals
    or after a user-defined delay
    Fn_P - The name of the function which is to be scheduled.

    NOTE: All scheduled functions must be 'void, void' -
    that is, they must take no parameters, and have
    a void return type.

    DELAY - The interval (TICKS) before the task is first executed

    PERIOD - If 'PERIOD' is 0, the function is only called once,
    at the time determined by 'DELAY'. If PERIOD is non-zero,
    then the function is called repeatedly at an interval
    determined by the value of PERIOD (see below for examples
    which should help clarify this)

    RETURN VALUE:
    Returns the position in the task array at which the task has been
    added. If the return value is SCHEDULER_MAX_TASKS then the task could
    not be added to the array (there was insufficient space). If the
    return value is < SCHEDULER_MAX_TASKS, then the task was added
    successfully.
    Note: this return value may be required, if a task is
    to be subsequently deleted - see schedulerDeleteTask().

    EXAMPLES:
    Task_ID = schedulerAddTask(Do_X,1000,0);
    Causes the function Do_X() to be executed once after 1000 sch ticks.
    Task_ID = schedulerAddTask(Do_X,0,1000);
    Causes the function Do_X() to be executed regularly, every 1000 sch ticks.
    Task_ID = schedulerAddTask(Do_X,300,1000);
    Causes the function Do_X() to be executed regularly, every 1000 ticks.
    Task will be first executed at T = 300 ticks, then 1300, 2300, etc.
*/
uint32_t schedulerAddTask (void (*pFunction)(void *ctx, uint32_t ticks),
                           void *context, uint32_t delay, uint32_t period)
{
    uint32_t i = 0;
    // First find a gap in the array (if there is one)
    while (i < SCHEDULER_MAX_TASKS && schedulerTasks[i].pTask)
    {
        i ++;
    }

    // Have we reached the end of the list?
    if (i == SCHEDULER_MAX_TASKS)
    {
        // Task list is full
        // Set the global error variable
        errorCode = 2; // ERROR_schedulerTOO_MANYTasks;
        // Also return an error code
        return SCHEDULER_MAX_TASKS;
    }

    sTask_t* t = &schedulerTasks[i];
    t->pTask     = pFunction;
    t->context   = context;
    t->delay     = delay;
    t->period    = period;
    t->runMe     = 0;

    return i;
}


int8_t schedulerModifyTaskPeriod (uint32_t taskIndex, uint32_t newPeriod)
{
    if (taskIndex >= SCHEDULER_MAX_TASKS)
    {
        return -1;
    }

    if (!schedulerTasks[taskIndex].pTask)
    {
        return -1;
    }

    schedulerTasks[taskIndex].period = newPeriod;
    return 0;
}
/*
    Removes a task from the scheduler. Note that this does
    *not* delete the associated function from memory:
    it simply means that it is no longer called by the scheduler.

    taskIndex - The task index. Provided by schedulerAddTask().

    RETURN VALUE: RETURN_ERROR or RETURN_NORMAL
*/
int8_t schedulerDeleteTask (uint32_t taskIndex)
{
    if (taskIndex >= SCHEDULER_MAX_TASKS)
    {
        return -1;
    }

    int8_t returnCode;

    if (schedulerTasks[taskIndex].pTask == 0)
    {
        // No task at this location...
        // Set the global error variable
        errorCode = 2; // ERROR_SCH_CANNOT_DELETE_TASK;
        // ...also return an error code
        returnCode = -1; // RETURN_ERROR;
    }
    else
    {
        returnCode = 0; // RETURN_NORMAL;
    }

    memset (&schedulerTasks[taskIndex], 0, sizeof(sTask_t));
    return returnCode; // return status
}


/*
    Simple function to display error codes.
    This version displays code on a port with attached LEDs:
    adapt, if required, to report errors over serial link, etc.
    Errors are only displayed for a limited period
    (60000 ticks = 1 minute at 1ms tick interval).
    After this the the error code is reset to 0.
    This code may be easily adapted to display the last
    error 'for ever': this may be appropriate in your
    application.
*/
void schedulerReportStatus( void )
{
#ifdef SCH_REPORT_ERRORS
   // ONLY APPLIES IF WE ARE REPORTING ERRORS
   // Check for a new error code
   if( errorCode != lastErrorCode ){

      // Negative logic on LEDs assumed
      errorPort = 255 - errorCode;
      lastErrorCode = errorCode;

      if( errorCode!= 0 ){
         errorTickCount = 60000;
      }
      else{
         errorTickCount = 0;
      }
   }
   else{
      if( errorTickCount != 0 ){
         if( --errorTickCount == 0 ){
            errorCode = 0; // Reset error code
         }
      }
   }
#endif
}
