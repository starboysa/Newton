#include "newton.h"
#include <iostream>
#include <thread>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                            Multi-Platform Error checking helpers
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef WIN32
#define NEWTON_SEND_SD SD_SEND

class WindowsErrChecker
{
public:
  void check(int lhs, int lineNum, const char* filename)
  {
    m_err = lhs;
    if (lhs == -1)
    {
      int actualError = WSAGetLastError();
      std::cout << "Error at " << filename << ":" << lineNum << " " << actualError << std::endl;
    }
  }

  int getReturnVal() const { return m_err; }

  Newton::ReturnCode ToReturnCode()
  {
    // TODO: impl
  }

private:
  int m_err = -1;
};

static WindowsErrChecker err;

#else
#define NEWTON_SEND_SD SHUT_WR

class LinuxErrChecker
{
public:
  void check(int lhs, int lineNum, const char* filename)
  {
    m_err = lhs;
  }

  int getReturnVal() const { return m_err; }

  Newton::ReturnCode ToReturnCode()
  {
    // TODO: impl
  }
  
private:
  int m_err = -1;
};

static LinuxErrChecker err;

#endif

// Add line and file information, it won't ever be a different file, but
// if included in a bigger project this'll be useful.
#define E(X) err.check(X, __LINE__, __FILE__)

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                     IPv4Rep Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::IPV4Rep Newton::IPV4Rep::DNSLookup(std::string dns_str, std::string service)
{
  addrinfo hint;
  memset(&hint, 0, sizeof(addrinfo));
  hint.ai_family = AF_INET;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_protocol = IPPROTO_TCP;

  addrinfo* pAddrs;

  int ret = getaddrinfo(dns_str.c_str(), service.c_str(), &hint, &pAddrs);

  sockaddr_in remote; // = static_cast<sockaddr_in*>(calloc(1, sizeof(sockaddr_in)));
  memcpy(&remote, pAddrs->ai_addr, sizeof(sockaddr_in));

  return IPV4Rep{ remote };
}

Newton::IPV4Rep Newton::IPV4Rep::IPAddr(std::string ip, int port)
{
  sockaddr_in remote; // = static_cast<sockaddr_in*>(calloc(1, sizeof(sockaddr_in)));
  remote.sin_family = AF_INET;
  remote.sin_port = htons(port);

#if WIN32
  InetPton(AF_INET, ip.c_str(), &remote.sin_addr);
#else
  inet_pton(AF_INET, ip.c_str(), &remote.sin_addr);
#endif

  return IPV4Rep{ remote };
}


///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                       AutoReturnCodeReactor Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::AutoReturnCodeReactor::AutoReturnCodeReactor(bool printString, HandleStrategy hs) : m_print(printString), m_strategy(hs)
{
}

void Newton::AutoReturnCodeReactor::operator=(ReturnCode rc)
{
  // TODO: impl
}


///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                              Newton Utility Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

void Newton::PrintLastError()
{
  // TODO: impl
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                           Newton Maitenence Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::ReturnCode Newton::Initilize()
{
#ifdef WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#else

#endif

  return ReturnCode::OK;
}

Newton::ReturnCode Newton::Clean()
{
#ifdef WIN32
  E(WSACleanup());
#endif

  return ReturnCode::OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                               Newton Socket Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::Socket Newton::CreateSocket()
{
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  return { sock };
}

Newton::Socket Newton::CreateSocket(int port)
{
  Socket sock = CreateSocket();

  sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = inet_addr("127.0.0.1");
  service.sin_port = htons(port);

  E(bind(sock.s, (sockaddr*)&service, sizeof(sockaddr_in)));

  return sock;
}

Newton::ReturnCode Newton::CloseSocket(Socket s)
{
  // free(s.connection);

#ifdef WIN32
  E(closesocket(s.s));
#endif

  return ReturnCode::OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                               Newton Client Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::ReturnCode Newton::ConnectSocketTo(Socket& s, IPV4Rep to)
{

  s.connection = to.remote;

  // Since we're a client, we don't have to bind a port manually.
  E(connect(s.s, (sockaddr*)&to.remote, sizeof(sockaddr_in)));

  return ReturnCode::OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                 Newton Host Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::ReturnCode Newton::Host(Socket s, int max_connections, std::shared_ptr<DataRecieverFactory> data_reciever_factory, bool verbose)
{
  std::thread([=](){
    BlockingHost(s, max_connections, data_reciever_factory, verbose);
  }).detach();

  return ReturnCode::OK;
}

Newton::ReturnCode Newton::BlockingHost(Socket s, int max_connections, std::shared_ptr<DataRecieverFactory> data_reciever_factory, bool verbose)
{
  E(listen(s.s, max_connections));

  socklen_t psize = sizeof(sockaddr_in);
  sockaddr_in incoming;

  if(verbose)
    std::cout << "Awaiting Connections..." << std::endl;

  std::atomic<bool> continueService(true);

  while (continueService)
  {
    SOCKET sock = accept(s.s, (sockaddr*)&incoming, &psize);

    if(verbose)
      std::cout << "Recieved Connection!" << std::endl;

    Socket connection_sock;
    connection_sock.s = sock;
    connection_sock.connection = incoming;

    DataRecieverFactory::DataRecieverArgs dra(connection_sock, continueService);
    std::thread([=]() // Spawn a new thread for this client
    {
      std::shared_ptr<DataReciever> data_reciever = data_reciever_factory->MakeDataReciever(dra);

      BlockingExpectData(connection_sock, data_reciever); // No need to spawn a new thread, we're already on a new thread
    }).detach();
  }

  return ReturnCode::OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                 Newton Both Impl
///////////////////////////////////////////////////////////////////////////////////////////////////

Newton::ReturnCode Newton::SendData(Socket s, std::shared_ptr<DataSender> d)
{
  ByteBuffer buff = d->ConvertToBytes();
  int dataSize = buff.m_size;
  const byte* data = buff.m_ptr;
  E(send(s.s, data, dataSize, 0));

  return ReturnCode::OK;
}

void Newton::ExpectData(Socket s, std::shared_ptr<DataReciever> from)
{
  std::thread([=](){
    BlockingExpectData(s, from);
  }).detach();
}

void Newton::BlockingExpectData(Socket s, std::shared_ptr<DataReciever> from)
{
  // 2048 is closest power of 2 over the 1500 byte MTU
  byte buff[2048];
  E(recv(s.s, buff, 2048 - 1, 0));
  unsigned size = err.getReturnVal();
  bool expect = true;

  while(expect)
  {
    if (size != -1)
    {
      from->OnPacketRecieved();

      if (size == 0)
      {
        expect = from->OnFinRecieved();
      }
      else
      {
        expect = from->InterpretBytes({ buff, size });
      }
    }
  }
}

Newton::ReturnCode Newton::ShutdownOutput(Socket s)
{
  E(shutdown(s.s, NEWTON_SEND_SD));

  return ReturnCode::OK;
}