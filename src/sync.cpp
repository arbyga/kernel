/**************************************************************************************

Copyright © 2004-2010 VMware, Inc. All rights reserved.

Written by Mark Venguerov 2004 - 2010

**************************************************************************************/

#include "sync.h"
#include "session.h"

using namespace MVStoreKernel;

SpinC SpinC::SC;

#ifdef POSIX
volatile long Mutex::fInit = 0;
pthread_mutexattr_t Mutex::mutexAttrs;

Mutex::Mutex()
{
	while (fInit<=0)
		if (!cas(&fInit,0,-1)) threadYield();
		else {
			pthread_mutexattr_init(&mutexAttrs);
			pthread_mutexattr_settype(&mutexAttrs,PTHREAD_MUTEX_DEFAULT);
			pthread_mutexattr_setpshared(&mutexAttrs,PTHREAD_PROCESS_PRIVATE);
			//pthread_mutexattr_setprotocol(&mutexAttrs,PTHREAD_PRIO_NONE);
			fInit=1; break;
		}
	pthread_mutex_init(&mutex,&mutexAttrs);
}
#endif

SimpleSem::~SimpleSem()
{
	// ??? release all waiting
}

void SimpleSem::lock(SemData& dt)
{
	while (!casP((void* volatile *)&chain,(void*)(dt.next=chain),(void*)&dt));
	if (dt.next!=NULL) {dt.wait(); dt.next=NULL;}
}

bool SimpleSem::trylock(SemData& dt)
{
	dt.next=NULL; return casP((void* volatile *)&chain,(void*)0,(void*)&dt);
}

void SimpleSem::unlock(SemData& dt)
{
	assert(dt.next==NULL);
	if (!casP((void* volatile *)&chain,(void*)&dt,(void*)0))
		for (SemData *pd=chain; ;pd=pd->next) if (pd->next==&dt) {pd->wakeup(); break;}
}

SimpleSemNC::~SimpleSemNC()
{
	// ??? release all waiting
}

void SimpleSemNC::lock(SemData& dt)
{

	for (;;dt.wait()) {
		while (!casP((void* volatile *)&chain,(void*)(dt.next=chain),(void*)&dt));
		if (dt.next==NULL) return;
	}
}

bool SimpleSemNC::trylock(SemData& dt)
{
	dt.next=NULL; return casP((void* volatile *)&chain,(void*)0,(void*)&dt);
}

bool SimpleSemNC::waitlock(SemData& dt,RWLock *lock)
{
	while (!casP((void* volatile *)&chain,(void*)(dt.next=chain),(void*)&dt));
	if (dt.next==NULL) return true;
	if (lock!=NULL) lock->unlock();
	dt.wait(); dt.next=NULL;
	if (lock!=NULL) lock->lock(RW_X_LOCK);
	return false;
}

void SimpleSemNC::unlock(SemData& dt)
{
	SemData *pd,*pd2; assert(dt.next==NULL && chain!=NULL);
	for (pd=chain; !casP((void* volatile *)&chain,(void*)pd,(void*)0); pd=chain);
	for (; pd!=&dt; pd=pd2) {pd2=pd->next; pd->wakeup();}
}

Semaphore::~Semaphore()
{
	// ??? release all waiting
}

void Semaphore::lock(SemData& dt)
{

	while (owner!=&dt) {
		while (!casP((void* volatile *)&chain,(void*)(dt.next=chain),(void*)&dt));
		if (dt.next==NULL) {owner=&dt; break;}
		dt.wait(); dt.next=NULL;
	}
	InterlockedIncrement(&recCnt);
}

bool Semaphore::trylock(SemData& dt)
{
	dt.next=NULL;
	if (owner==&dt || casP((void* volatile *)&chain,(void*)0,(void*)&dt))
		{InterlockedIncrement(&recCnt); owner=&dt; return true;}
	return false;
}

void Semaphore::unlock(SemData& dt,bool fWakeupAll)
{
	assert(recCnt>0 && owner==&dt && dt.next==NULL);
	if (InterlockedDecrement(&recCnt)==0) {
		SemData *pd,*pd2; owner=NULL;
		if (fWakeupAll) {
			for (pd=chain; !casP((void* volatile *)&chain,(void*)pd,(void*)0); pd=chain);
			for (; pd!=&dt; pd=pd2) {pd2=pd->next; pd->wakeup();}
		} else if (!casP((void* volatile *)&chain,(void*)&dt,(void*)0)) {
			assert(chain!=NULL); for (pd=chain; pd->next!=&dt; pd=pd->next);
			owner=pd; pd->wakeup();
		}
	}
}

void RWLock::wait(SemData *q) {
	assert(ptrdiff_t(queue)==ptrdiff_t(~0ULL) && ptrdiff_t(q)!=ptrdiff_t(~0ULL));
	Session *ses=Session::getSession(); SemData sd,&sem=ses!=NULL?ses->lockReq.sem:sd;
	sem.next=q; queue=&sem; sem.wait(); sem.next=NULL;
}

#ifdef __arm__
bool __sync_bool_compare_and_swap_8(volatile void* destination, long long unsigned comperand, long long unsigned exchange)                 
{
int lRes = 0;  
   __asm__ __volatile__
   (
   	   " ldrexd r4,r5, [%3]   \n"   //block the access to destination...
	   " ldrd r2,r3,%1        \n"	//r2,r3 == comperand itself...
	   " cmp  r4,r2	          \n" 
	   " bne  L3_%=           \n"
	   " cmp  r5,r3			  \n"
	   "L3_%=:" " strned r4, %1 \n"
	   " mov %0, #1    \n"
       " bne E_%=      \n"
       " ldreqd r4, r5, %2  		\n"
	   " strexdeq %0, r4, [%3]  \n"	//unblock the access to destination...
	   "E_%=: \n"
      : "=&r"(lRes)
      : "m"(comperand),
        "m"(exchange),
        "r"(destination)
      :"cc", "r0", "r1", "r2","r3", "r4", "r5"
   );
  return lRes ? 0 : 1; 
}
#endif