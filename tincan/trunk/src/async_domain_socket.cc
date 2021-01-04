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

//Creates a socket with the SOCKET_PATH_NAME and creates an epoll instance
AsyncDomainSocket::AsyncDomainSocket(const char* socket_filename)
{
  //Create socket, setsockopt for credential check, bind with SOCKET_PATH_NAME
  socket_fd_ = this->Create(socket_filename);
  if (socket_fd_ == -1) {
    RTC_LOG(LS_ERROR) << "Error creating socket\n";
    return;
  }

  //Create an epoll instance
  epoll_fd_ = epoll_create1(0);
  if(epoll_fd_ == -1)
  {
    RTC_LOG(LS_ERROR) << "Failed to create epoll file descriptor\n";
    return;
  }
}

void
AsyncDomainSocket::StartSocketFunction ()
{
  //Starting std::thread skt_thread_ to accept client and receive data
  skt_thread_ = std::thread(&AsyncDomainSocket::SocketRun, this);
}

//Returns false only if epoll_wait fails or if exit_condition is set to true from control_listener
bool
AsyncDomainSocket::EpollWaitFor()
{
  if (skt_stop_) {
    RTC_LOG(LS_WARNING) << "exit condition set to true, exiting\n";
    return false;
  }

  event_count = epoll_wait(epoll_fd_, events, 0x100, -1); //MAX_EVENTS, timeOut=(-1->indefinite, 0->none)
  if (event_count == -1) {
    int errsv = errno;
    RTC_LOG(LS_WARNING) << "epoll_wait failed with error: " << errsv <<"\n";
    return false;
  }

  return true;
}

//Returns the status of the epoll_ctl call on deregistering the client_fd_
int
AsyncDomainSocket::RemoveClientFromEpoll(int cfd) {
  int status = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, cfd, &event); 
  return status;
}


void
AsyncDomainSocket::SocketRun()
{
  //Listens with queue limit of 1 client
  int status = this->Listen(socket_fd_);
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Error listening socket\n";
    return;
  }

  //Accepts client connection
  int cfd = Accept();
  if (cfd == -1) {
    RTC_LOG(LS_ERROR) << "Accept failed\n";
    return;
  }

  //Adds client_fd_ to epoll instance
  int efd = AddClientToEpoll(cfd);
  if (efd == -1) {
    RTC_LOG(LS_ERROR) << "AddClientToEpoll failed\n";
    return;
  }

  char* buffer = new char[1500];

  //start reading data
  while(EpollWaitFor()) {
    for (int e = 0; e < event_count; ++e) {
      //Client disconnect
      if((events[e].events & EPOLLERR) || (events[e].events & EPOLLHUP) || (!(events[e].events & EPOLLIN))) {
        RTC_LOG(LS_WARNING) << "Disconnection detected\n";
	int status = RemoveClientFromEpoll(cfd);
	if (status == -1) {
	  RTC_LOG(LS_ERROR) << "Issue removing client fd from epoll instance\n";
	  return;
	}
      //New client connection update to epoll
      } else if (events[e].data.fd == socket_fd_) {
        int cfd = Accept();
        if (cfd == -1) {
          RTC_LOG(LS_ERROR) << "Accept failed\n";
          return;
        }

	int efd = AddClientToEpoll(cfd);
        if (efd == -1) {
          RTC_LOG(LS_ERROR) << "AddClientToEpoll failed\n";
          return;
        }

      //Data In
      } else if (events[e].events & EPOLLIN) {
        //read data from recv call
	 int recvd = Receive(cfd, buffer);
         if (recvd > 0) {
           this->SignalRecv(buffer, sizeof(buffer));
         } else {
	   RTC_LOG(LS_ERROR) << "Receive failed\n";
	   return;
	 }

      //Data Out
      } else if (events[e].events & EPOLLOUT) {
      
      } 
    }
  }
  Close();
  return;
}

//Creates the socket, sets SO_PASSCRED and binds to the passed socket_filename
int
AsyncDomainSocket::Create(const char* socket_filename)
{
  // Creating socket
  int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (fd == -1) {
    RTC_LOG(LS_ERROR) << "Creation of socket failed\n";
    return fd;
  }

  /* We must set the SO_PASSCRED socket option in order to receive
       credentials */
  
  int optval = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
    RTC_LOG(LS_ERROR) << "Error with setsockopt\n";
    return -1;
  }

  memset(&bind_addr_, 0, sizeof(bind_addr_));

  //Bind socket to socket path
  bind_addr_.sun_family = AF_UNIX;
  strncpy(bind_addr_.sun_path, socket_filename, sizeof(bind_addr_.sun_path)-1);

  int status = bind(fd, reinterpret_cast<sockaddr*>(&bind_addr_), sizeof(bind_addr_));
  if (status == -1) {
    RTC_LOG(LS_ERROR) << "Binding of socket failed\n";
    return status;
  }
  return fd;
}

//Listens on socket_fd_ with queue length of 1
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

//Returns client_fd_ of the accepted client socket fd on success
int
AsyncDomainSocket::Accept()
{
  client_fd_ = accept4(socket_fd_, nullptr, nullptr, SOCK_NONBLOCK);
  if (client_fd_ == -1) {
    RTC_LOG(LS_ERROR) << "Accept failure\n";
  }
  return client_fd_;
}

//Adds the client_fd_ to epoll_fd_, epoll instance has been created at the contructor of this class.
int
AsyncDomainSocket::AddClientToEpoll(int cfd)
{
  event.data.fd = cfd;
  /* EPOLLIN - for read op, EPOLLET - for edge triggered notification */
  event.events = EPOLLIN | EPOLLET; //May need to add more flags to watch out for
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &event)) {
    RTC_LOG(LS_ERROR) << "Failed to add listening file descriptor to epoll\n";
    close(epoll_fd_);
    return -1;
  }
  return epoll_fd_; 
}

void
AsyncDomainSocket::Quit()
{
  Close();
  skt_stop_ = true;
  skt_thread_.join();
}

void
AsyncDomainSocket::Close(){
  close(epoll_fd_);
  close(client_fd_);
}

AsyncDomainSocket::~AsyncDomainSocket(){
  close(socket_fd_);
  //If somehow the join was missed, calling as part of Close
  if (skt_thread_.joinable()) {
    skt_thread_.join();
  }

}

} //end namespace tincan




