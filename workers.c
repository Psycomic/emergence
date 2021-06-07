#include "misc.h"

#include <stdio.h>
#include <stdbool.h>

#ifdef __linux__
#include <pthread.h>
extern void exit(int status);

#endif

#ifdef _WIN32
#include <windows.h>
#endif

struct Worker;

typedef struct {
	struct Worker* self;
	void* fn_data;
} WorkerData;

typedef int (*FWorker)(WorkerData*);
typedef void (*Signal)(void*);

#define MAX_SIGNAL_QUEUE_SIZE 256

#define THREAD_FINISHED (1 << 0)

#define mutex_lock(m) /* printf("Lock at %s:%d\n", __FILE__, __LINE__); */ pthread_mutex_lock(m)
#define mutex_unlock(m) /* printf("Unlock at %s:%d\n", __FILE__, __LINE__); */ pthread_mutex_unlock(m)

typedef struct Worker {
	WorkerData data;
	uint8_t flags;
	FWorker worker_fun;
	int return_code;

	struct {
		Signal signal;
		void* data;
	} signal_queue[MAX_SIGNAL_QUEUE_SIZE];

	uint32_t signal_queue_size;

#ifdef __linux__
	pthread_t handle;
	pthread_mutex_t queue_mutex;
#endif
#ifdef _WIN32
	HANDLE handle;
	HANDLE queue_mutex;
	DWORD id;
#endif
} Worker;

#define _THREAD_INTERNAL
#include "workers.h"
#undef _THREAD_INTERNAL

#ifdef __linux__
typedef void* thread_wrapper_return_t;
#endif
#ifdef _WIN32
typedef DWORD thread_wrapper_return_t;
#endif

thread_wrapper_return_t thread_fn_wrapper(void* data) {
	WorkerData* w_data = data;
	Worker* self = w_data->self;

	self->return_code = self->worker_fun(w_data);

	self->flags |= THREAD_FINISHED;

#ifdef __linux__
	pthread_exit(NULL);
#endif
#ifdef _WIN32
	CloseHandle(self->handle);
    CloseHandle(self->queue_mutex);
#endif
}

Worker* worker_create(FWorker runner_fun, void* data) {
	Worker* worker = malloc(sizeof(Worker));
	worker->worker_fun = runner_fun;
	worker->data.fn_data = data;
	worker->data.self = worker;
	worker->flags = 0;

	worker->signal_queue_size = 0;

	#ifdef __linux__
	int m_error = pthread_mutex_init(&worker->queue_mutex, NULL);
	int error = pthread_create(&worker->handle, NULL, thread_fn_wrapper, &worker->data);

	if (error != 0 || m_error != 0) {
		fprintf(stderr, "Error in thread creation!\n");
		exit(-1);
	}
	#endif
	#ifdef _WIN32
	worker->queue_mutex = CreateMutex(NULL, FALSE, NULL);
	worker->handle = CreateThread(NULL, 0, thread_fn_wrapper, &worker->data, 0, &worker->id);

	if (worker->handle == NULL || worker->queue_mutex == NULL) {
		fprintf(stderr, "Error in thread creation!\n");
		exit(-1);
	}
	#endif

	return worker;
}

int worker_return_code(Worker* worker) {
	if (!(worker->flags & THREAD_FINISHED)) {
		fprintf(stderr, "Thread is not finished!\n");
		return -1;
	}
	else {
		return worker->return_code;
	}
}

void worker_lock_queue(Worker* worker) {
#ifdef __linux__
	mutex_lock(&worker->queue_mutex);
#endif
#ifdef _WIN32
	DWORD dwWaitResult = WaitForSingleObject(worker->queue_mutex, INFINITE);

	if (dwWaitResult == WAIT_ABANDONED) {
		fprintf(stderr, "Mutex error!\n");
	}
#endif
}

void worker_unlock_queue(Worker* worker) {
#ifdef __linux__
	mutex_unlock(&worker->queue_mutex);
#endif
#ifdef _WIN32
	if (!ReleaseMutex(worker->queue_mutex)) {
		fprintf(stderr, "Mutex error!\n");
	}
#endif
}

void worker_update(Worker* worker) {
	if (worker->signal_queue_size > 0) {
		worker_lock_queue(worker);

		for (uint i = 0; i < worker->signal_queue_size; i++) {
			worker->signal_queue[i].signal(worker->signal_queue[i].data);
		}

		worker->signal_queue_size = 0;
		worker_unlock_queue(worker);
	}
}

void worker_emit(WorkerData* data, Signal signal, void* signal_data) {
	Worker* self = data->self;

	worker_lock_queue(self);

	self->signal_queue[self->signal_queue_size].signal = signal;
	self->signal_queue[self->signal_queue_size].data = signal_data;

	self->signal_queue_size++;

	if (self->signal_queue_size >= MAX_SIGNAL_QUEUE_SIZE) {
		fprintf(stderr, "Too many emits!\n");
		exit(-1);
	}

	worker_unlock_queue(self);
}

bool worker_finished(Worker* worker) {
	if (worker->flags & THREAD_FINISHED) {
		worker_update(worker);
		return true;
	}
	else {
		return false;
	}
}
