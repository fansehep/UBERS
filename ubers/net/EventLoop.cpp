#include <sys/eventfd.h>
#include <signal.h>
#include "EventLoop.h"
#include "Channel.h"
#include "Epoll.h"
#include "TimerManager.h"

using namespace UBERS;
using namespace UBERS::net;
namespace
{
thread_local EventLoop* t_LoopInthisThread = nullptr;
int CreateEventFd()
{
  int event_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if(event_fd < 0)
  {
    LOG_OFF << "eventfdCreat fail";
  }
  return event_fd;
}
class Ignore_SigPipe
{
public:
  Ignore_SigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
  }
};
Ignore_SigPipe InitObject;
}

EventLoop::EventLoop()
  :quit_(false),
   threadId_(CurrentThread::tid()),
   epoll_(std::make_unique<Epoll>(this)),
   timermanager_(std::make_unique<TimerManager>(this)),
   wakeupFd_(CreateEventFd()),
   wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_))
{
  if(t_LoopInthisThread)
  {
    LOG_FATAL << "another EventLoop" << t_LoopInthisThread << "existed in this Thread" << threadId_;
  }
  else
  {
    t_LoopInthisThread = this;
  }
  wakeupChannel_->SetReadCallBack([this]{HandleRead();});
  wakeupChannel_->EnableEvents(kReadEventET);
}   

EventLoop::~EventLoop()
{
  wakeupChannel_->DisableAll();
  wakeupChannel_->Remove();
  ::close(wakeupFd_);
  t_LoopInthisThread = nullptr;
}

void EventLoop::Loop()
{
  assert(!looping_);
  AssertInLoopThread();
  looping_ = true;
  quit_ = false;
  while(!quit_)
  {
    ActiveChannels_.clear();
    epoll_->Poll(&ActiveChannels_);
    eventHandling_ = true;
    for(Channel* channel : ActiveChannels_)
    {
      channel->HandleEvent();
    }
    eventHandling_ = false;
    RunFunction();
  }
}

void EventLoop::Quit()
{
  quit_ = true;
  if(!isInLoopThread())
  {
    WakeUp();
  }
}

void EventLoop::RunInLoop(const std::function<void()> func)
{
  if(isInLoopThread())
  {
    func();
  }
  else
  {
    QueueInLoop(func);
  }
}

void EventLoop::QueueInLoop(const std::function<void()> func)
{
  {
    std::scoped_lock<std::mutex> l_guard(mutex_);
    functions_.emplace_back(func);
  }
  if(!isInLoopThread() || CallFunction_)
  {
    WakeUp();
  }
}

std::weak_ptr<Timer> EventLoop::RunAt(const TimeStamp time, std::function<void()> func)
{
  return timermanager_->AddTimer(std::move(func), time, 0.0);
}
std::weak_ptr<Timer> EventLoop::RunEvery(const double interval, std::function<void()> func)
{
  TimeStamp time(addTime(TimeStamp::now(), interval));
  return timermanager_->AddTimer(std::move(func), time, interval);
}
std::weak_ptr<Timer> EventLoop::RunAfter(const double interval, std::function<void()> func)
{
  TimeStamp time(addTime(TimeStamp::now(), interval));
  return RunAt(time, std::move(func));
}

void EventLoop::WakeUp() const
{
  uint64_t a = 1;
  if(::write(wakeupFd_, &a, sizeof(a)) != sizeof(a))
  {
    LOG_ERROR << "eventLoop::WakeUp() writes" << "instead of 8 bytes";
  }
}

void EventLoop::UpdateChannel(Channel* channel)
{
  assert(channel->GetOwnerLoop() == this);
  AssertInLoopThread();
  epoll_->RemoveChannel(channel);
}

void EventLoop::AbortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this \
            << " was created in threadId_ " << threadId_ \
            << ", current thread Id = " << CurrentThread::tid();
}

void EventLoop::RemoveChannel(Channel* channel)
{
  assert(channel->GetOwnerLoop() == this);
  AssertInLoopThread();
  epoll_->RemoveChannel(channel);
}

void EventLoop::CreateConnction(int Sockfd, const ConnectionCallBack& connectioncallback, \
                                 const MessageCallBack& messagecallback,  \
                                 const WriteCompleteCallBack& writecompletecallback)
{
  epoll_->CreateConnection(Sockfd, connectioncallback, messagecallback, writecompletecallback);
}

void EventLoop::HandleRead() const
{
  uint64_t a = 1;
  if(::write(wakeupFd_, &a, sizeof(a)) != sizeof(a))
  {
    LOG_ERROR << "EventLoop::Wakeup() read " << "another bytes instead of 8 bytes";
  }
}

void EventLoop::RunFunction()
{
  std::vector<std::function<void()>> functions;
  CallFunction_ = true;

  {
    std::scoped_lock<std::mutex> l_guard(mutex_);
    functions.swap(functions_);
  }

  for(const auto& func : functions)
  {
    func();
  }
  CallFunction_ = false;
}