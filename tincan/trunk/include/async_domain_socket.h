/*
* EdgeVPNio
* Copyright 2020, University of Florida
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/
#ifndef UNIX_SOCKET_DOMAIN_H_
#define UNIX_SOCKET_DOMAIN_H_
#include "tincan_base.h"

#include "rtc_base/third_party/sigslot/sigslot.h"
#include "rtc_base/logging.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>


namespace tincan {
class AsyncDomainSocket : 
  public sigslot::has_slots<>
{
public:
  static const char* const SOCKET_PATH_NAME;
  AsyncDomainSocket(const char* socket_filename);
  ~AsyncDomainSocket();
  void StartSocketFunction();
  void Quit();
  sigslot::signal2<const char* , size_t> SignalRecv;
 // sigslot::signal1<const char*, size_t> SignalSend;
private:

  int Create(const char* socket_filename);
  int Listen(int fd_);
  int Accept(); //Return epoll_fd_ created from epoll_ctl
  void Close();
  int Receive(int client_fd_, char* buf);
  int Send(int client_fd_, char* buf);
  void SocketRun(); //Function that starts executing as std:thread is instantiated

  int socket_fd_, client_fd_, epoll_fd_;
  struct epoll_event event, events[5];
  struct sockaddr_un bind_addr_;
  std::mutex uskt_mutex_; //mutex used to lock and synchronize conditional variable for exit condition
  std::condition_variable skt_cond_; //Conditional variable to notify the exit condition with Quit() from ctrl_listener
  bool skt_stop_ = false; //condition used to exit startSocketFunction
  std::thread skt_thread_; //thread on which async_socket_server accepts client connection and receives data

};
} //namespace tincan
#endif  // UNIX_SOCKET_DOMAIN_H_

