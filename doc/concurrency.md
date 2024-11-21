# Morelibc
## Concurrency
Morelibc models the system as a single process with multiple threads. A single process because microcontrollers typically don't have virtual memory management, and it avoids supporting C/POSIX process-related functions. Threads are implemented as FreeRTOS tasks.

All of the FreeRTOS API can be used as-is in Morelibc. You can create threads using `xTaskCreate`, you can create mutexes, queues, task notifications, anything that FreeRTOS supports continues to work the same. In fact, Morelibc doesn't define any concurrency primitives, and using FreeRTOS directly is the way to do concurrency control. It is possible to have a pthreads wrapper library around FreeRTOS, but that is beyond the scope of Morelibc.

That said Morelibc does provide some concurrency-related abstractions.

### Threads
Morelibc introduces an optional thread abstraction to support task cancellation. In Unix systems, syscalls can be interrupted by a signal (e.g., `SIGINT` from pressing Ctrl-C). This behavior is mimicked in Morelibc to support the REPL use case (e.g., in MicroPython). In the REPL use case, the user has an interactive shell to the microcontroller. The user enters a command which starts some processing but then the processing stalls because it is blocked waiting for input from somewhere. The user now wants to abort the command by pressing Ctrl-C, and this should promptly cause the processing to stop and return the the interactive shell.

To implement this, when Ctrl-C is pressed, there will be a thread stuck indefinitely in a blocked state, and we need to get that thread out of its blocked state and gracefully continue running to clean itself up. Fortunately FreeRTOS already offers a function, [`xTaskAbortDelay`](https://www.freertos.org/Documentation/02-Kernel/04-API-references/02-Task-control/09-xTaskAbortDelay), which unblocks a task. 

However we can't go around aborting tasks arbitrarily without destabilizing the system. Most FreeRTOS code doesn't expect to be randomly aborted. For example, common FreeRTOS code looks like:
```
xSemaphoreTake(mutex, portMAX_DELAY);
// do something with mutex held
```
This code doesn't check the return value of `xSemaphoreTake` because it, quite reasonably, doesn't expect it to fail. However if this task is aborted while waiting to acquire the mutex, `xSemaphoreTake` will return with an error value, and the code after it will keep running even though the mutex is not held. Because it is difficult and uncommon to write code that can be asynchronously aborted, the ability to abort a task must be opt-in, rather than ambient.

Allowing opt-in task cancellation is the purpose of the Morelibc thread abstraction. Morelibc threads are created using `thread_create`.
```
#include "morelib/thread"

thread_t *thread = thread_create(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority);
```
The parameters are the same as `xTaskCreate`, but it returns a `thread_t` object which wraps the `TaskHandle_t` and provides cancellation support. Temporarily allowing a task to be cancelled is modelled after enabling/disabling interrupts.
```
if (thread_enable_interrupt() < 0) {
    return -1;
}
uint32_t ret = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
thread_disable_interrupt();
return ret ? ret : -1;
```
A thread starts a cancellation block by calling `thread_enable_interrupt`. If it returns a negative value, then the thread was already marked to be interrupted and `errno` has been set to `EINTR`. In this case the thread should not start a blocking call and should immediately propagate the error indicating that the operation was interrupted. 

If `thread_enable_interrupt` returns a non-negative value then thread "interrupts" are enabled for this thread. Any subsequent blocking call may fail due to a thread "interrupt" being sent to this thread. Ordinarily the call to `ulTaskNotifyTake` would wait forever until the task is notified, but if the thread is interrupted it will return early without the task having been notified.

After the blocking call, `thread_disable_interrupt` disables thread interrupts, which is the normal state. You usually only want very specific blocking call sites to be interruptible, and most of those call sites are inside Morelibc, and it would be uncommon for application code to need to do this dance. For example, all the blocking calls in Morelibc (e.g., `read`, `poll`, `sleep`) already handle this and behave like "syscalls" that can return `EINTR`, just like they would in a real Unix system.

To actually interrupt a thread, call `thread_interrupt`.
```
thread_interrupt(thread);
```
Applications are expected to call this function. For example in a REPL application, the Ctrl-C handler will call `thread_interrupt` to interrupt the REPL thread to get it to abort processing.

Using Morelibc threads is entirely optional; it is still valid to create raw FreeRTOS tasks. The only difference is that those tasks cannot respond to thread interrupts, which may be what you want anyway.

### Asyncio, `poll` and `select`
Poll, epoll, kqueue, IO completion ports are abstractions that different operating systems use to allow programs to wait for multiple events at once. Morelibc implements the poll abstraction since it seems simpler and more widely understood than the others. `select` can then be implemented on top of any of these abstractions.

Programs can use `poll` to wait on multiple file descriptors at once. This allows one thread to handle multiple connections instead of having one thread for each connection. The file descriptors being waited on are typically sockets, pipes, or serial connections, since they have asynchronous behavior.
