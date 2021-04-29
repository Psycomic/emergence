#ifndef _THREAD_HEADER
#define _THREAD_HEADER

#ifndef _THREAD_INTERNAL
#include <stdbool.h>

typedef void WorkerData;
typedef void Worker;

typedef int (*FWorker)(WorkerData*);
typedef void (*Signal)();
#endif

Worker* worker_create(FWorker runner_fun, void* data);
bool worker_finished(Worker* worker);
int worker_return_code(Worker* worker);
void worker_emit(WorkerData* data, Signal signal, void* signal_data);
void worker_update(Worker* worker);

#endif
