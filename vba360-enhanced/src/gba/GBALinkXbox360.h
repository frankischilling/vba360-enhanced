#pragma once

#include <vector>
#include "SFML/Network.hpp"
#include "../common/Types.h"

struct LanLinkXboxConfig
{
  bool active;
  bool connected;
  bool terminate;
  bool speed;
  int type;
  int numgbas;
  u16 port;
};

class GBALinkXbox360Server
{
public:
  GBALinkXbox360Server();
  ~GBALinkXbox360Server();

  bool Start(u16 port = 5738, int expectedPlayers = 1);
  bool AcceptPending();
  bool BroadcastFrame(const std::vector<u16>& payload, u8 tspeed, int savedLinkTime);
  bool ReceiveFrame(std::vector<u16>& payload, u8& tspeed, int& savedLinkTime);
  void Stop();

  bool IsConnected() const;
  int ConnectedClients() const;

private:
  bool SendToClient(sf::SocketTCP& client, const std::vector<char>& packet);
  bool ReceivePacket(sf::SocketTCP& client, std::vector<char>& buffer);
  void CloseClients();

  LanLinkXboxConfig config;
  sf::SocketTCP listener;
  std::vector<sf::SocketTCP> clients;
};

class GBALinkXbox360Client
{
public:
  GBALinkXbox360Client();
  ~GBALinkXbox360Client();

  bool Connect(const sf::IPAddress& serverAddr, u16 port = 5738);
  bool SendFrame(const std::vector<u16>& payload, u8 tspeed, int savedLinkTime);
  bool ReceiveFrame(std::vector<u16>& payload, u8& tspeed, int& savedLinkTime);
  void Disconnect();

  bool IsConnected() const;

private:
  bool ReceivePacket(std::vector<char>& buffer);
  bool SendPacket(const std::vector<char>& packet);

  LanLinkXboxConfig config;
  sf::SocketTCP socket;
  sf::IPAddress serverAddress;
};

std::vector<char> BuildLinkPacket(const std::vector<u16>& payload, u8 tspeed, int savedLinkTime);
bool ParseLinkPacket(const std::vector<char>& buffer, std::vector<u16>& payload, u8& tspeed, int& savedLinkTime);
