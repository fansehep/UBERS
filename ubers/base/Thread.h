/*
* 对线程类的封装, 虽然c++11 已经引入了 thread 类
*
*/
#ifndef _UBERS_THREAD_H_
#define _UBERS_THREAD_H_
#include <thread>
#include <string>
#include <functional>
#include <atomic>
#include <boost/noncopyable.hpp>
#include <boost/thread/latch.hpp>
namespace UBERS
{
class Thread : public boost::noncopyable
{
public:
  using ThreadFunc = std::function<void()>;  
  explicit Thread(ThreadFunc);
  ~Thread();
  void Join() { this->thread_->join(); }
  void Detach() { this->thread_->detach();}
  bool Joinable() { return this->thread_->joinable(); }
  void Stop() { this->thread_->request_stop();}
  void start();
  [[nodiscard]] bool started() const { return started_; }
private:
  bool started_ = false;
  std::unique_ptr<std::jthread> thread_;
  pid_t tid_ = 0;
  ThreadFunc func_;
  boost::latch latch_;
};
}
#endif