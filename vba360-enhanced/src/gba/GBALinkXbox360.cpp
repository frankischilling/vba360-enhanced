#include "GBALinkXbox360.h"

namespace
{
  const std::size_t kMaxPacketSize = 256;

  LanLinkXboxConfig MakeDefaultConfig()
  {
    LanLinkXboxConfig cfg;
    cfg.active = false;
    cfg.connected = false;
    cfg.terminate = false;
    cfg.speed = false;
    cfg.type = 0;
    cfg.numgbas = 0;
    cfg.port = 5738;
    return cfg;
  }
}

GBALinkXbox360Server::GBALinkXbox360Server()
{
  config = MakeDefaultConfig();
}

GBALinkXbox360Server::~GBALinkXbox360Server()
{
  Stop();
}

bool GBALinkXbox360Server::Start(u16 port, int expectedPlayers)
{
  Stop();

  config = MakeDefaultConfig();
  config.active = true;
  config.connected = false;
  config.numgbas = expectedPlayers;
  config.port = port;

  if (listener.Listen(port) != sf::Socket::Done)
    return false;

  listener.SetBlocking(false);
  return true;
}

bool GBALinkXbox360Server::AcceptPending()
{
  if (!config.active)
    return false;

  sf::SocketTCP next;
  sf::Socket::Status status = listener.Accept(next);

  if (status == sf::Socket::Done)
  {
    next.SetBlocking(false);
    clients.push_back(next);
  }

  if (config.numgbas > 0 && (int)clients.size() >= config.numgbas)
    config.connected = true;

  return config.connected;
}

bool GBALinkXbox360Server::BroadcastFrame(const std::vector<u16>& payload, u8 tspeed, int savedLinkTime)
{
  if (!config.connected || clients.empty())
    return false;

  std::vector<char> packet = BuildLinkPacket(payload, tspeed, savedLinkTime);
  bool ok = true;

  for (std::size_t i = 0; i < clients.size(); i++)
    ok = SendToClient(clients[i], packet) && ok;

  return ok;
}

bool GBALinkXbox360Server::ReceiveFrame(std::vector<u16>& payload, u8& tspeed, int& savedLinkTime)
{
  if (!config.connected)
    return false;

  for (std::size_t i = 0; i < clients.size(); i++)
  {
    std::vector<char> buffer;
    if (ReceivePacket(clients[i], buffer) && ParseLinkPacket(buffer, payload, tspeed, savedLinkTime))
      return true;
  }

  return false;
}

void GBALinkXbox360Server::Stop()
{
  CloseClients();
  listener.Close();
  config = MakeDefaultConfig();
}

bool GBALinkXbox360Server::IsConnected() const
{
  return config.connected;
}

int GBALinkXbox360Server::ConnectedClients() const
{
  return (int)clients.size();
}

bool GBALinkXbox360Server::SendToClient(sf::SocketTCP& client, const std::vector<char>& packet)
{
  if (packet.empty())
    return false;

  return client.Send(&packet[0], packet.size()) == sf::Socket::Done;
}

bool GBALinkXbox360Server::ReceivePacket(sf::SocketTCP& client, std::vector<char>& buffer)
{
  char raw[kMaxPacketSize];
  std::size_t received = 0;
  sf::Socket::Status status = client.Receive(raw, sizeof(raw), received);

  if (status == sf::Socket::NotReady || received == 0)
    return false;

  if (status != sf::Socket::Done)
    return false;

  buffer.assign(raw, raw + received);
  return true;
}

void GBALinkXbox360Server::CloseClients()
{
  for (std::size_t i = 0; i < clients.size(); i++)
    clients[i].Close();

  clients.clear();
}

GBALinkXbox360Client::GBALinkXbox360Client()
{
  config = MakeDefaultConfig();
}

GBALinkXbox360Client::~GBALinkXbox360Client()
{
  Disconnect();
}

bool GBALinkXbox360Client::Connect(const sf::IPAddress& serverAddr, u16 port)
{
  Disconnect();

  config = MakeDefaultConfig();
  config.active = true;
  config.connected = false;
  config.port = port;

  if (serverAddr.IsValid())
    serverAddress = serverAddr;
  else
    serverAddress = sf::IPAddress::LocalHost;

  if (socket.Connect(port, serverAddress) != sf::Socket::Done)
    return false;

  socket.SetBlocking(false);
  config.connected = true;
  config.numgbas = 1;
  return true;
}

bool GBALinkXbox360Client::SendFrame(const std::vector<u16>& payload, u8 tspeed, int savedLinkTime)
{
  if (!config.connected)
    return false;

  std::vector<char> packet = BuildLinkPacket(payload, tspeed, savedLinkTime);
  return SendPacket(packet);
}

bool GBALinkXbox360Client::ReceiveFrame(std::vector<u16>& payload, u8& tspeed, int& savedLinkTime)
{
  if (!config.connected)
    return false;

  std::vector<char> buffer;
  if (!ReceivePacket(buffer))
    return false;

  return ParseLinkPacket(buffer, payload, tspeed, savedLinkTime);
}

void GBALinkXbox360Client::Disconnect()
{
  socket.Close();
  config = MakeDefaultConfig();
}

bool GBALinkXbox360Client::IsConnected() const
{
  return config.connected;
}

bool GBALinkXbox360Client::ReceivePacket(std::vector<char>& buffer)
{
  char raw[kMaxPacketSize];
  std::size_t received = 0;
  sf::Socket::Status status = socket.Receive(raw, sizeof(raw), received);

  if (status == sf::Socket::NotReady || received == 0)
    return false;

  if (status != sf::Socket::Done)
    return false;

  buffer.assign(raw, raw + received);
  return true;
}

bool GBALinkXbox360Client::SendPacket(const std::vector<char>& packet)
{
  if (packet.empty())
    return false;

  return socket.Send(&packet[0], packet.size()) == sf::Socket::Done;
}

std::vector<char> BuildLinkPacket(const std::vector<u16>& payload, u8 tspeed, int savedLinkTime)
{
  std::vector<char> packet;
  std::size_t needed = 1 + 1 + sizeof(savedLinkTime) + payload.size() * sizeof(u16);

  if (needed > kMaxPacketSize)
    needed = kMaxPacketSize;

  packet.reserve(needed);
  packet.push_back((char)needed);
  packet.push_back((char)tspeed);

  for (int shift = 0; shift < 32 && packet.size() < needed; shift += 8)
    packet.push_back((char)((savedLinkTime >> shift) & 0xff));

  for (std::size_t i = 0; i < payload.size() && packet.size() + 1 < needed; i++)
  {
    u16 value = payload[i];
    packet.push_back((char)(value & 0xff));
    if (packet.size() < needed)
      packet.push_back((char)((value >> 8) & 0xff));
  }

  return packet;
}

bool ParseLinkPacket(const std::vector<char>& buffer, std::vector<u16>& payload, u8& tspeed, int& savedLinkTime)
{
  if (buffer.size() < 2)
    return false;

  std::size_t declared = (u8)buffer[0];
  if (declared == 0 || declared > buffer.size())
    return false;

  if (declared < 1 + 1 + sizeof(savedLinkTime))
    return false;

  tspeed = (u8)buffer[1];

  savedLinkTime = 0;
  for (int i = 0; i < 4; i++)
  {
    savedLinkTime |= (int)((unsigned char)buffer[2 + i]) << (8 * i);
  }

  payload.clear();
  for (std::size_t offset = 6; offset + 1 < declared; offset += 2)
  {
    u16 value = (u16)((unsigned char)buffer[offset] | ((unsigned char)buffer[offset + 1] << 8));
    payload.push_back(value);
  }

  return true;
}
