#ifndef Tools_hpp
#define Tools_hpp
/* Some useful defines for the project */

#include "freertos/FreeRTOS.h"


/* IPC task priority is 24, so TASK_REALTIME_PRIORITY would be 20 */
#define TASK_LOW_PRIORITY      5
#define TASK_NORMAL_PRIORITY   10
#define TASK_HIGH_PRIORITY     15
#define TASK_REALTIME_PRIORITY 20

/**
 * Converts day of week from US format (a week starts from Sunday)
 * to ISO 8601 format (a week starts from Monday)
 */
#define CONVERT_DOWS(x) ((x) == 0 ? 6 : (x) - 1)

/* Returns non-const pointer to std::string buffer */
#define CSTR(x) (&(x)[0])

/**
 * Returns a function that takes a pointer to the object,
 * casting it to pointer to `Class` object and invoking its method `Method`.
 * Needed for creating FreeRtos tasks from object methods
 * Without NOINLINE_ATTR debugger crashes while trying to get the stack
 */
template<class Class, void (Class::*Method)()> TaskFunction_t methodToTaskFun()
{
    return [](void *self) -> void NOINLINE_ATTR {
        (static_cast<Class *>(self)->*Method)();
    };
}

#endif // #ifdef Tools_hpp