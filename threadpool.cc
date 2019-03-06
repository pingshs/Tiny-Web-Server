#include"threadpool.h"

void ThreadPool::start(int numthread){
	assert(numthread > 0);

	threadid_.resize(numthread);
	running_ = true;
	for(int i = 0; i < numthread; i++)
		pthread_create(&threadid_[i], NULL, ThreadProc, static_cast<void*>(this));
}

void ThreadPool::stop(){
	{
		MutexLockGuard lock(mutex_);
		running_ = false;
	}
	cond_.notifyAll();
	
	for(auto& thread : threadid_)	
		pthread_join(thread, NULL);
}

void ThreadPool::put(Task&& task){
	{
		MutexLockGuard lock(mutex_);
		taskqueue_.push_back(std::move(task));
	}
	cond_.notify();
}

ThreadPool::Task ThreadPool::take(){
	Task task;
	MutexLockGuard lock(mutex_);
	while(taskqueue_.empty() && running_)
		cond_.wait();
	if(!taskqueue_.empty()){
		task = taskqueue_.front();
		taskqueue_.pop_front();
	}
	return task;
}

void* ThreadPool::ThreadProc(void *args){
	ThreadPool* pObject = static_cast<ThreadPool*>(args);
	while(pObject->running_){
		Task task(pObject->take());
		task();
	}

	return NULL;
}
