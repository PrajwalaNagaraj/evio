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
#include "async_domain_socket.h"

namespace tincan
{
using namespace rtc;
const char* const AsyncDomainSocket::SOCKET_PATH_NAME = "/evio/unix_socket_domain/2011038.socket";
//Creates a socket with the SOCKET_PATH_NAME and starts listening.
AsyncDomainSocket::AsyncDomainSocket(const char* socket_filename)
{
  //Create socket, setsockopt for credential check, bind with SOCKET_PATH_NAME
  socket_fd_ = this->Create(socket_filename);
  if (socket_fd_ == -1) {
    RTC_LOG(LS_ERROR) << "Error creating socket\n";
    return;
  }

  //Listens with queue limit of 1 client
  int status = this->Listen(socket_fd_);
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Error listening socket\n";
    return;
  }
}

void
AsyncDomainSocket::StartSocketFunction ()
{
  /*char* buffer = new char[1500];
  client_fd_ = Accept();
  std::unique_lock<std::mutex> lg(uskt_mutex_);
  while (!skt_stop_) {
    int status = Receive(client_fd_, buffer);
    //TODO:Error handling if receive fails
   try {
      TincanControl ctrl(buffer, len);
      RTC_LOG(LS_INFO) << "Received CONTROL: " << ctrl.StyledString();
      (*ctrl_dispatch_)(ctrl);
    } catch(exception & e) {
      RTC_LOG(LS_WARNING) << "A control failed to execute.\n"
      << string(data, len) << "\n"
      << e.what();
    }
    skt_cond_.wait(lg);
  }*/
  //Starting std::thread skt_thread_ to accept client and receive data
  skt_thread_ = std::thread(&AsyncDomainSocket::SocketRun, this);

}

void
AsyncDomainSocket::SocketRun()
{
  char* buffer = new char[1500];
  int efd_ = Accept();
  if (efd_ == -1) {
    RTC_LOG(LS_ERROR) << "Accept failed\n";
    return;
  }
//  int recvd = 1;
    //int r = 1;
    //int n;
  std::unique_lock<std::mutex> lg(uskt_mutex_);
  while (!skt_stop_) {
    //specifying a timeout equal to zero cause epoll_wait() to return
    // immediately, even if no events are available
    int event_count = epoll_wait(efd_, events, 0x100, 0); //MAX_EVENTS, timeOut
    if (event_count == -1) {
       int errsv = errno;
       RTC_LOG(LS_WARNING) << "epoll_wait failed with error: " << errsv <<"\n";
       if (errsv == EPOLLHUP || errsv == EPOLLERR) {
	  //connection to client broken, reconnect
	  efd_ = Accept();
	  if (efd_ == -1) {
    	    RTC_LOG(LS_ERROR) << "Accept failed\n";
            return;
          }	
	  event_count = epoll_wait(efd_, events, 0x100, 0); //MAX_EVENTS, timeOut
       }
    }
	  
    for (int e = 0; e < event_count; ++e)
    {
      int recvd = Receive(client_fd_, buffer);
      if (recvd > 0)
      {
	 this->SignalRecv(buffer, sizeof(buffer));
         //send(cfd, &message, recvd, MSG_NOSIGNAL);
         --e;
      }
    }
    skt_cond_.wait(lg); //loops untill quit is signalled
  }
}

void
AsyncDomainSocket::Quit()
{
  lock_guard<mutex> lg(uskt_mutex_);
  skt_stop_ = true;
  skt_cond_.notify_one();
  skt_thread_.join();
}

int
AsyncDomainSocket::Create(const char* socket_filename)
{
  // Creating socket
  int fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (fd_ == -1) {
    RTC_LOG(LS_ERROR) << "Creation of socket failed\n";
    return fd_;
  }

  /* We must set the SO_PASSCRED socket option in order to receive
       credentials */
  
  int optval = 1;
  if (setsockopt(fd_, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
    RTC_LOG(LS_ERROR) << "Error with setsockopt\n";
    return -1;
  }

  memset(&bind_addr_, 0, sizeof(bind_addr_));

  //Bind socket to socket path
  bind_addr_.sun_family = AF_UNIX;
  strncpy(bind_addr_.sun_path, socket_filename, sizeof(bind_addr_.sun_path)-1);

  int status = bind(fd_, reinterpret_cast<sockaddr*>(&bind_addr_), sizeof(bind_addr_));
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Binding of socket failed\n";
    return status;
  }
  return fd_;
}

int
AsyncDomainSocket::Listen(int socket_fd_) 
{
  int status = listen(socket_fd_, 1);
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Listen failure\n";
  }
  return status;
}

//Receives data for the passed client connected fd, within the buffer
int
AsyncDomainSocket::Receive(int cfd, char* buffer)
{
  int status = recv(cfd, &buffer, sizeof(buffer), 0);
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Receive data failure\n";
  }
  return status;
}

//Sends data to the mentioned client connected fd with passed buffer
int
AsyncDomainSocket::Send(int cfd, char* buffer)
{
  int status = send(cfd,  &buffer, sizeof(buffer), MSG_NOSIGNAL);
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Send data failure\n";
  }
  return status;
}

//Returns epoll_fd_ built on accepted client socket fd on success
int
AsyncDomainSocket::Accept()
{
  client_fd_ = accept4(socket_fd_, nullptr, nullptr, SOCK_NONBLOCK);
  if (client_fd_ == -1) {
    RTC_LOG(LS_ERROR) << "Accept failure\n";
  } else {
    epoll_fd_ = epoll_create1(0);
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd_, &event))
    {
      RTC_LOG(LS_ERROR) << "Failed to add file descriptor to epoll\n";
      close(epoll_fd_);
      return -1;
    }
  }
  return epoll_fd_;
}

void
AsyncDomainSocket::Close(){
  close(epoll_fd_);
  close(client_fd_);
}

AsyncDomainSocket::~AsyncDomainSocket(){
  close(socket_fd_);
}

} //end namespace tincan




