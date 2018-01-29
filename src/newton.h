/**************************************************************************************************
*
* \file newton.h
*
* <h1>Name Explination</h1>
* Network
* Execution
* Wrapper
* That
* Operates
* Nonblockingly
*
* Also Multi-Platform
*
* <h1>Purpose</h1>
* Newton is a networking library that abstracts the usage of Berkley API 
* on Windows and Linux behind a C++ friendly OOP layer while still revealing
* the useful part of the low-levelness of the API.  Specifically:  Newton abstracts
* creating TCP/IPv4 connections while still allowing the user to manipulate the
* low-level data that is put on the wire which makes it useful for implementing
* published Web APIs like HTTP.  Functions marked Blocking do not spawn a new thread,
* all these functions have non-blocking alternatives.
*
* <h1>Highlighted Design Differences from Berkley API</h1>
* The Berkley API follows a send-recieve model, but because Newton is non-blocking
* busy waiting for recieved data is not an option.  Instead, Newton uses a send-expect
* model.  This means you can tell Newton to expect to data to be recieved and it will
* spawn a thread to wait for the data while you continue on with your day.
*
* <h1><b>!! WARNING !!</b></h1>
* Newton makes use of threads!  Make sure any code written in DataSenders, DataRecievers,
* and DataRecieverFactories is all thread-safe!!!
*
**************************************************************************************************/

#pragma once

#ifdef WIN32

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Ws2tcpip.h>

#else

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#endif

#ifndef WIN32

using SOCKET = int;

#endif

#include <string>
#include <memory>
#include <atomic>

namespace Newton
{
  class DataRecieverFactory;
  class DataReciever;
  class DataSender;

  struct Socket
  {
    SOCKET s;
    sockaddr_in connection;
  };

  // IPv4 Representative, wrapper around sockaddr_in and also contains
  // helper functions for all useful ways to look up IPv4s
  struct IPV4Rep
  {
    static IPV4Rep DNSLookup(std::string dns_str, std::string service);
    static IPV4Rep IPAddr(std::string ip, int port);

    sockaddr_in remote;
  };

  // Newton Error codes
  enum class ReturnCode
  {
    OK
  };

  // Class that automatically reacts to newton's error codes.
  // Useful for quick development, but probably shouldn't be used in
  // production.  I would polymorphize this and let the user pass it in
  // if the Windows error checking wasn't so different from the Linux
  // error checking :(
  class AutoReturnCodeReactor
  {
  public:
    enum class HandleStrategy
    {
      NONE,
      ASSERT,
      EXCEPTION
    };

    AutoReturnCodeReactor(bool printString, HandleStrategy hs);

    void operator=(ReturnCode rc);

    // Exception for EXCEPTION HandleStrategy
    class NewtonException : public std::exception
    {
    public:
    };

  private:
    bool m_print;
    HandleStrategy m_strategy;
  };

  // Utility
  void PrintLastError();

  // Maitenence
  ReturnCode Initilize();
  ReturnCode Clean();

  // Sockets
  Socket CreateSocket();
  Socket CreateSocket(int port);
  ReturnCode CloseSocket(Socket s);

  // Client Functions
  ReturnCode ConnectSocketTo(Socket& s, IPV4Rep to);
  ReturnCode ShutdownOutput(Socket s);

  // Host Functions  
  // NOTE: BlockingHost still spawns new threads for each client
  ReturnCode BlockingHost(Socket s, int max_connections,
    std::shared_ptr<DataRecieverFactory> data_reciever, bool verbose = false);

  ReturnCode Host(Socket s, int max_connections, 
    std::shared_ptr<DataRecieverFactory> data_reciever, bool verbose = false);

  // Functions for both
  ReturnCode SendData(Socket s, std::shared_ptr<DataSender> d);
  void BlockingExpectData(Socket s, std::shared_ptr<DataReciever> form);
  void ExpectData(Socket s, std::shared_ptr<DataReciever> form);

  // I feel like 'byte' better explains what we're sending rather than 'char'.
  using byte = char;
  struct ByteBuffer
  {
    const byte* m_ptr;
    unsigned m_size;
  };

  struct WriteableByteBuffer
  {
    byte* m_ptr;
    unsigned m_size;
  };

  // Pure virtual class that creates the ByteBuffer that is put on the wire.
  // Inherit from this class and implement the ConvertToBytes() function.
  class DataSender
  {
  public:
    virtual ~DataSender() = default;

    // Worker Threads
    virtual ByteBuffer ConvertToBytes() = 0;
  };

  // Pure virtual class that processes recieved data.  Also recieves TCP
  // state updates (FIN packets).
  class DataReciever
  {
  public:
    virtual ~DataReciever() = default;

    // All of these are run on a seperate thread, please make them thread safe.
    // They return whether or not to continue expecting data.
    virtual bool InterpretBytes(WriteableByteBuffer data) = 0;
    virtual bool OnFinRecieved() = 0;
    virtual void OnPacketRecieved() = 0;
  };

  // Pure virtual class that creates a DataReciever.  Only used by host processes.
  class DataRecieverFactory
  {
  public:
    virtual ~DataRecieverFactory() = default;

    // Arguments to pass to MakeDataReciever.  Making these a class rather than adding parameters
    // to MakeDataReciever means that most of these arguments can be ignored by the DataRecieverFactor
    // with the singular exception of the Socket.
    struct DataRecieverArgs
    {

      DataRecieverArgs(Socket s, std::atomic<bool>& host_ender) : m_s(s), m_host_ender(host_ender) {}
      DataRecieverArgs(const DataRecieverArgs& dra) : m_s(dra.m_s), m_host_ender(dra.m_host_ender) {}

      Socket m_s;
      std::atomic<bool>& m_host_ender;
    };

    virtual std::shared_ptr<DataReciever> MakeDataReciever(DataRecieverArgs args) = 0;
  };
}
