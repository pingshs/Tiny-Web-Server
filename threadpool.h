#include<vector>
#include<deque>
#include<functional>

#include<pthread.h>

//#define NDEBUG
#include<assert.h>

#include"mutex.h"

class ThreadPool{
public:
	typedef std::function<void()> Task;

//TODO
	ThreadPool():mutex_(), cond_(mutex_), running_(false){}
	~ThreadPool(){
		if(running_)
			stop();
	}

	void start(int numthread);
	void stop();

	void put(Task&& task);

	Task take();

private:
	std::vector<pthread_t> threadid_;
	std::deque<Task> taskqueue_;

	MutexLock mutex_;
	Condition cond_;

	bool running_;	

	static void* ThreadProc(void *args);
};
/*
void ThreadPool::start(int numthread){
	assert(numthread <= 0);

	threadid_.resize(numthread);
	for(int i = 0; i < numthread; i++)
		pthread_create(&threadid_[i], NULL, ThreadProc, static_cast<void*>(this));
	running_ = true;
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
	Task task(pObject->take());
	task();

	return NULL;
}

*/
