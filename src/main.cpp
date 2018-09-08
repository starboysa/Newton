#include <string>
#include <cstring>
#include <iostream>
#include <atomic>
#include <thread>
#include "newton.h"
#include <mutex>
#include <queue>
#include <sstream>

std::mutex g_accessToPrint;
std::queue<std::string> g_toPrint;

class HTTPGETRequestSender final : public Newton::DataSender
{
public:
  HTTPGETRequestSender(std::string host, std::string location) : m_d(nullptr), m_host(host), m_location(location) {}
  ~HTTPGETRequestSender() { delete[] m_d; }

  Newton::ByteBuffer ConvertToBytes() override
  {
    std::string HTTP_HEADER;
    HTTP_HEADER += "GET " + m_location + " HTTP/1.1\n";
    HTTP_HEADER += "Host: " + m_host + "\n\r\n\r\n";

    m_d = new char[HTTP_HEADER.length() + 1];
    std::strcpy(m_d, HTTP_HEADER.c_str());

    return { m_d, HTTP_HEADER.length() };
  }

private:
  char* m_d;
  std::string m_host, m_location;
};

class HTTPRequestResponseReciever final : public Newton::DataReciever
{
public:
  HTTPRequestResponseReciever(std::function<void(std::string)> onPacketRecieved) : m_onPacketRecieved(onPacketRecieved) {}

  bool InterpretBytes(Newton::WriteableByteBuffer data) override
  {
    char* msg = data.m_ptr;
    msg[data.m_size] = 0;

    m_onPacketRecieved(msg);
    m_buff += msg;

    return true;
  }

  bool OnFinRecieved() override
  {
    return false;
  }

  void OnPacketRecieved() override
  {
  }

  std::string m_buff;
private:
  std::function<void(std::string)> m_onPacketRecieved;
};


class ProxyForwarding final : public Newton::DataSender
{
public:
  ProxyForwarding(std::string buf) : m_buf(buf) {}

  Newton::ByteBuffer ConvertToBytes() override
  {
    return { m_buf.c_str(), m_buf.length() };
  }

private:
  std::string m_buf;
};

class ProxyRequestRecieved final : public Newton::DataReciever
{
public:
  ProxyRequestRecieved(Newton::Socket s) : m_s(s)
  {
      
  }

  bool InterpretBytes(Newton::WriteableByteBuffer data) override
  {
    char* msg = data.m_ptr;
    msg[data.m_size] = 0;

    m_buff += msg;

    int header_end = m_buff.find("\r\n\r\n");

    if(header_end != std::string::npos)
    {
      SendHTTPRequest();
    }

    return true;
  }

  void SendHTTPRequest()
  {
    // Get host field
    int from_line = m_buff.find("Host: ");
    int from_line_end = m_buff.find_first_of("\r\n", from_line);
    std::string host = m_buff.substr(from_line + 6, from_line_end - (from_line + 6));

    // Send HTTP request
    Newton::AutoReturnCodeReactor err(true, Newton::AutoReturnCodeReactor::HandleStrategy::ASSERT);
    Newton::Socket s = Newton::CreateSocket();

    Newton::IPV4Rep rep = Newton::IPV4Rep::DNSLookup(host, "http");
    err = Newton::ConnectSocketTo(s, rep);

    err = Newton::SendData(s, std::make_shared<ProxyForwarding>(m_buff));
    err = Newton::ShutdownOutput(s);

    Newton::BlockingExpectData(s, std::make_shared<HTTPRequestResponseReciever>([=](std::string str)
    {
      Newton::SendData(m_s, std::make_shared<ProxyForwarding>(str));
      using namespace std::chrono_literals;
      // std::this_thread::sleep_for(500ms); // Forced lag for multithreading tests.
    }));

    Newton::CloseSocket(s);
    Newton::CloseSocket(m_s);
  }

  bool OnFinRecieved() override
  {
    return true;
  }

  void OnPacketRecieved() override
  {
  }

private:
  Newton::Socket m_s;
  std::string m_buff;
};

class ProxyRequestRecievedFactory final : public Newton::DataRecieverFactory
{
public:
  std::shared_ptr<Newton::DataReciever> MakeDataReciever(DataRecieverArgs args) override
  {
    return std::make_shared<ProxyRequestRecieved>(args.m_s);
  }
};

int main(int argc, char** argv)
{
  Newton::AutoReturnCodeReactor err(true, Newton::AutoReturnCodeReactor::HandleStrategy::ASSERT);

  err = Newton::Initilize();
  
#if 0
  Newton::Socket s = Newton::CreateSocket();
  Newton::IPV4Rep rep = Newton::IPV4Rep::IPAddr("127.0.0.1", 80);
  Newton::ConnectSocketTo(s, rep);

  
  Newton::SendData(s, std::make_shared<HTTPGETRequestSender>("www.bbc.com", "/"));


  Newton::BlockingExpectData(s, std::make_shared<HTTPRequestResponseReciever>([](std::string httpPacket) {
    std::cout << httpPacket;
  }));

#else

  Newton::Socket s = Newton::CreateSocket(80);
  Newton::BlockingHost(s, SOMAXCONN, std::make_shared<ProxyRequestRecievedFactory>(), true); // Host is listening so it is blocking.

#endif

  err = Newton::CloseSocket(s);
  err = Newton::Clean();

  return 0;
}
