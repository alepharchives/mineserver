/*
   Copyright (c) 2010, The Mineserver Project
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
 * Neither the name of the The Mineserver Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#ifdef WIN32
  #include <conio.h>
  #include <winsock2.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <string.h>
  #include <netdb.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <cassert>
#include <deque>
#include <map>
#include <iostream>
#include <event.h>
#include <ctime>
#include <vector>
#include <zlib.h>
#include <signal.h>

#include "constants.h"
#include "mineserver.h"

#include "logger.h"

#include "sockets.h"
#include "tools.h"
#include "map.h"
#include "user.h"
#include "chat.h"
#include "mapgen.h"
#include "config.h"
#include "nbt.h"
#include "packets.h"
#include "physics.h"

#ifdef WIN32
static bool quit = false;
#endif

int setnonblock(int fd)
{
  #ifdef WIN32
  u_long iMode = 1;
  ioctlsocket(fd, FIONBIO, &iMode);
  #else
  int flags;

  flags  = fcntl(fd, F_GETFL);
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
  #endif

  return 1;
}

//Handle signals
void sighandler(int sig_num)
{
  Mineserver::Get().Stop();
}

int main(int argc, char *argv[])
{
  signal(SIGTERM, sighandler);
  signal(SIGINT, sighandler);

  return Mineserver::Get().Run(argc, argv);
}

Mineserver::Mineserver()
{
}

event_base *Mineserver::GetEventBase()
{
  return m_eventBase;
}

int Mineserver::Run(int argc, char *argv[])
{
  uint32 starttime = (uint32)time(0);
  uint32 tick      = (uint32)time(0);

  initConstants();

  std::string file_config;
  file_config.assign(CONFIG_FILE);
  std::string file_admin;
  file_admin.assign(ADMIN_FILE);
  std::string file_items;
  file_items.assign(ITEMS_FILE);
  std::string file_motd;
  file_motd.assign(MOTD_FILE);
  std::string file_rules;
  file_rules.assign(RULES_FILE);

  if (argc > 1)
    file_config.assign(argv[1]);

  // Initialize conf
  Conf::get().load(file_config);

  // Load item aliases
  Conf::get().load(file_items);

  // Load admins
  Chat::get().loadAdmins(file_admin);
  // Load MOTD
  Chat::get().checkMotd(file_motd);

  Chat::get().loadBanned(BANNEDFILE);
  Chat::get().loadWhitelist(WHITELISTFILE);

  // Set physics enable state according to config
  Physics::get().enabled = (Conf::get().bValue("liquid_physics"));

  // Initialize map
  Map::get().initMap();

  // Initialize packethandler
  PacketHandler::get().initPackets();

  // Load ip from config
  std::string ip = Conf::get().sValue("ip");

  // Load port from config
  int port = Conf::get().iValue("port");

#ifdef WIN32
  WSADATA wsaData;
  int iResult;
  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if(iResult != 0)
  {
    printf("WSAStartup failed with error: %d\n", iResult);
    return EXIT_FAILURE;
  }
#endif

  struct sockaddr_in addresslisten;
  int reuse             = 1;

  m_eventBase = (event_base *)event_init();
#ifdef WIN32
  m_socketlisten = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  m_socketlisten = socket(AF_INET, SOCK_STREAM, 0);
#endif

  if(m_socketlisten < 0)
  {
    std::cerr << "Failed to create listen socket" << std::endl;
    return 1;
  }

  memset(&addresslisten, 0, sizeof(addresslisten));

  addresslisten.sin_family      = AF_INET;
  addresslisten.sin_addr.s_addr = inet_addr(ip.c_str());
  addresslisten.sin_port        = htons(port);

  setsockopt(m_socketlisten, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

  //Bind to port
  if(bind(m_socketlisten, (struct sockaddr *)&addresslisten, sizeof(addresslisten)) < 0)
  {
    std::cerr << "Failed to bind" << std::endl;
    return 1;
  }

  if(listen(m_socketlisten, 5) < 0)
  {
    std::cerr << "Failed to listen to socket" << std::endl;
    return 1;
  }


  setnonblock(m_socketlisten);
  event_set(&m_listenEvent, m_socketlisten, EV_WRITE|EV_READ|EV_PERSIST, accept_callback, NULL);
  event_add(&m_listenEvent, NULL);

  std::cout << std::endl<<
  "   _____  .__  "<<
  std::endl<<
  "  /     \\ |__| ____   ____   ______ ______________  __ ___________ "<<
  std::endl<<
  " /  \\ /  \\|  |/    \\_/ __ \\ /  ___// __ \\_  __ \\  \\/ // __ \\_  __ \\"<<
  std::endl<<
  "/    Y    \\  |   |  \\  ___/ \\___ \\\\  ___/|  | \\/\\   /\\  ___/|  | \\/"<<
  std::endl<<
  "\\____|__  /__|___|  /\\___  >____  >\\___  >__|    \\_/  \\___  >__|   "<<
  std::endl<<
  "        \\/        \\/     \\/     \\/     \\/                 \\/       "<<
  std::endl<<
  "Version " << VERSION <<" by Fador & Nredor"<<
  std::endl << std::endl;

  if(ip == "0.0.0.0")
  {
    std::cout << "Listening on port " << port << std::endl;
    // Print all local IPs
    char name[255];
    gethostname ( name, sizeof(name));
    struct hostent *hostinfo = gethostbyname(name);
    std::cout << "Server IP(s): ";
    int ipIndex = 0;
    while(hostinfo->h_addr_list[ipIndex]) {
        if(ipIndex > 0) { std::cout << ", "; }
        char *ip = inet_ntoa(*(struct in_addr *)hostinfo->h_addr_list[ipIndex++]);
        std::cout << ip;
    }
    std::cout << std::endl;
  }
  else
  {
    std::cout << "Listening on " << ip << ":" << port << std::endl;
  }

  timeval loopTime;
  loopTime.tv_sec  = 0;
  loopTime.tv_usec = 200000; //200ms

  m_running=true;
  event_base_loopexit(m_eventBase, &loopTime);
  while(m_running && event_base_loop(m_eventBase, 0) == 0)
  {
    if(time(0)-starttime > 10)
    {
      starttime = (uint32)time(0);
      std::cout << "Currently " << Users.size() << " users in!" << std::endl;

      //If users, ping them
      if(Users.size() > 0)
      {
        //0x00 package
        uint8 data = 0;
        Users[0]->sendAll(&data, 1);

        //Send server time
        Packet pkt;
        pkt << (sint8)PACKET_TIME_UPDATE << (sint64)Map::get().mapTime;
        Users[0]->sendAll((uint8*)pkt.getWrite(), pkt.getWriteLen());        
      }

      //Try to load release time from config
      int map_release_time = Conf::get().iValue("map_release_time");

      //Release chunks not used in <map_release_time> seconds
      std::vector<uint32> toRelease;
      for(std::map<uint32, int>::const_iterator it = Map::get().mapLastused.begin();
          it != Map::get().mapLastused.end();
          ++it)
      {
        if(Map::get().mapLastused[it->first] <= time(0)-map_release_time)
          toRelease.push_back(it->first);
      }

      int x_temp, z_temp;
      for(unsigned i = 0; i < toRelease.size(); i++)
      {
        Map::get().idToPos(toRelease[i], &x_temp, &z_temp);
        Map::get().releaseMap(x_temp, z_temp);
      }
    }

    //Every second
    if(time(0)-tick > 0)
    {
      tick = (uint32)time(0);
      //Loop users
      for(unsigned int i = 0; i < Users.size(); i++)
      {
        Users[i]->pushMap();
        Users[i]->popMap();
      }
      Map::get().mapTime+=20;
      if(Map::get().mapTime>=24000) Map::get().mapTime=0;
    }

    //Physics simulation every 200ms
    Physics::get().update();

    event_base_loopexit(m_eventBase, &loopTime);
  }

  Map::get().freeMap();

  #ifdef WIN32
  closesocket(m_socketlisten);
  #else
    close(m_socketlisten);
  #endif

  return EXIT_SUCCESS;
}

bool Mineserver::Stop()
{
  m_running=false;

  return true;
};
