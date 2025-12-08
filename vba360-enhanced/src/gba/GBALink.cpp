// This file was written by denopqrihg

#ifndef _XBOX
#include "../win32/stdafx.h"
#include "../win32/VBA.h"
#else
#include <xtl.h>
#endif

#include "GBALink.h"
#include "GBASockClient.h"

#include "GBA.h"
#include <stdio.h>
#include "../common/port.h"
#ifndef _XBOX
#include "../win32/MainWnd.h"
#include "../win32/LinkOptions.h"
#include "../win32/Reg.h"
#endif

#define UPDATE_REG(address, value) WRITE16LE(((u16 *)&ioMem[address]),value)

static bool SendExact(sf::SocketTCP& socket, const char* data, std::size_t size)
{
	if (size == 0)
		return true;

	return socket.Send(data, size) == sf::Socket::Done;
}

static bool ReceiveExact(sf::SocketTCP& socket, char* data, std::size_t size)
{
	std::size_t receivedTotal = 0;
	while (receivedTotal < size)
	{
		std::size_t receivedNow = 0;
		sf::Socket::Status status = socket.Receive(data + receivedTotal, size - receivedTotal, receivedNow);
		if (status == sf::Socket::NotReady)
			return false;
		if (status != sf::Socket::Done)
			return false;
		receivedTotal += receivedNow;
	}
	return true;
}

// If disabled, gba core won't call any (non-joybus) link functions
bool gba_link_enabled = false;

// Joybus
bool gba_joybus_enabled = false;
GBASockClient* dol = NULL;
sf::IPAddress joybusHostAddr = sf::IPAddress::LocalHost;

// Hodgepodge
int linktime = 0;
u8 tspeed = 3;
u8 transfer = 0;
LINKDATA *linkmem = NULL;
int linkid = 0, vbaid = 0;
HANDLE linksync[4];
int savedlinktime = 0;
HANDLE mmf = NULL;
char linkevent[] = "VBA link event  ";
static int i, j;
int linktimeout = 1000;
LANLINKDATA lanlink;
u16 linkdata[4];
int lspeed = 0;
lserver ls;
lclient lc;
bool oncewait = false, after = false;

// RFU crap (except for numtransfers note...should probably check that out)
bool rfu_enabled = false;
u8 rfu_cmd, rfu_qsend, rfu_qrecv;
int rfu_state, rfu_polarity, linktime2, rfu_counter, rfu_masterq;
// numtransfers seems to be used interchangeably with linkmem->numtransfers
// probably a bug?
int rfu_transfer_end, numtransfers = 0;
u32 rfu_masterdata[32];

// ???
int trtimedata[4][4] = {
	{34080, 8520, 5680, 2840},
	{65536, 16384, 10923, 5461},
	{99609, 24903, 16602, 8301},
	{133692, 33423, 22282, 11141}
};

int trtimeend[3][4] = {
	{72527, 18132, 12088, 6044},
	{106608, 26652, 17768, 8884},
	{133692, 33423, 22282, 11141}
};

int gbtime = 1024;

int GetSIOMode(u16, u16);

#ifndef _XBOX
DWORD WINAPI LinkClientThread(void *);
DWORD WINAPI LinkServerThread(void *);
#endif

int StartServer(void);

u16 StartRFU(u16);

char *MakeInstanceFilename(const char *Input)
{
	if (vbaid == 0)
		return (char *)Input;

	static char *result=NULL;
	if (result!=NULL)
		free(result);

	result = (char *)malloc(strlen(Input)+3);
	char *p = strrchr((char *)Input, '.');
	sprintf(result, "%.*s-%d.%s", (int)(p-Input), Input, vbaid+1, p+1);
	return result;
}


void StartLink(u16 value)
{
	if (ioMem == NULL)
		return;

	if (rfu_enabled) {
		UPDATE_REG(COMM_SIOCNT, StartRFU(value));
		return;
	}

	switch (GetSIOMode(value, READ16LE(&ioMem[COMM_RCNT]))) {
	case MULTIPLAYER:
		if (value & 0x80) {
			if (!linkid) {
				if (!transfer) {
					if (lanlink.active)
					{
						if (lanlink.connected)
						{
							linkdata[0] = READ16LE(&ioMem[COMM_SIODATA8]);
							savedlinktime = linktime;
							tspeed = value & 3;
							ls.Send();
							transfer = 1;
							linktime = 0;
							UPDATE_REG(COMM_SIODATA32_L, linkdata[0]);
							UPDATE_REG(COMM_SIODATA32_H, 0xffff);
							WRITE32LE(&ioMem[0x124], 0xffffffff);
							if (lanlink.speed&&oncewait == false)
								ls.howmanytimes++;
							after = false;
						}
					}
					else if (linkmem->numgbas > 1)
					{
						ResetEvent(linksync[0]);
						linkmem->linkcmd[0] = ('M' << 8) + (value & 3);
						linkmem->linkdata[0] = READ16LE(&ioMem[COMM_SIODATA8]);

						if (linkmem->numtransfers != 0)
							linkmem->lastlinktime = linktime;
						else
							linkmem->lastlinktime = 0;

						if ((++linkmem->numtransfers) == 0)
							linkmem->numtransfers = 2;
						transfer = 1;
						linktime = 0;
						tspeed = value & 3;
						WRITE32LE(&ioMem[COMM_SIODATA32_L], 0xffffffff);
						WRITE32LE(&ioMem[0x124], 0xffffffff);
					}
				}
			}
			value &= 0xff7f;
			value |= (transfer != 0) << 7;
		}
		value &= 0xff8b;
		value |= (linkid ? 0xc : 8);
		value |= linkid << 4;
		UPDATE_REG(COMM_SIOCNT, value);
		if (linkid)
			UPDATE_REG(COMM_RCNT, 7);
		else
			UPDATE_REG(COMM_RCNT, 3);
		break;

	case NORMAL8:
	case NORMAL32:
	case UART:
	default:
		UPDATE_REG(COMM_SIOCNT, value);
		break;
	}
}

void StartGPLink(u16 value)
{
	UPDATE_REG(COMM_RCNT, value);

	if (!value)
		return;

	switch (GetSIOMode(READ16LE(&ioMem[COMM_SIOCNT]), value)) {
	case MULTIPLAYER:
		value &= 0xc0f0;
		value |= 3;
		if (linkid)
			value |= 4;
		UPDATE_REG(COMM_SIOCNT, ((READ16LE(&ioMem[COMM_SIOCNT])&0xff8b)|(linkid ? 0xc : 8)|(linkid<<4)));
		break;

	case GP:
		if (rfu_enabled)
			rfu_state = RFU_INIT;
		break;
	}
}

void JoyBusConnect()
{
	delete dol;
	dol = NULL;

	dol = new GBASockClient(joybusHostAddr);
}

void JoyBusShutdown()
{
	delete dol;
	dol = NULL;
}

void JoyBusUpdate(int ticks)
{
	linktime += ticks;
	static int lastjoybusupdate = 0;

	// Kinda ugly hack to update joybus stuff intermittently
	if (linktime > lastjoybusupdate + 0x3000)
	{
		lastjoybusupdate = linktime;

		char data[5] = {0x10, 0, 0, 0, 0}; // init with invalid cmd
		std::vector<char> resp;

		if (!dol)
			JoyBusConnect();

		u8 cmd = dol->ReceiveCmd(data);
		switch (cmd) {
		case JOY_CMD_RESET:
			UPDATE_REG(COMM_JOYCNT, READ16LE(&ioMem[COMM_JOYCNT]) | JOYCNT_RESET);

		case JOY_CMD_STATUS:
			resp.push_back(0x00); // GBA device ID
			resp.push_back(0x04);
			break;
		
		case JOY_CMD_READ:
			resp.push_back((u8)(READ16LE(&ioMem[COMM_JOY_TRANS_L]) & 0xff));
			resp.push_back((u8)(READ16LE(&ioMem[COMM_JOY_TRANS_L]) >> 8));
			resp.push_back((u8)(READ16LE(&ioMem[COMM_JOY_TRANS_H]) & 0xff));
			resp.push_back((u8)(READ16LE(&ioMem[COMM_JOY_TRANS_H]) >> 8));
			UPDATE_REG(COMM_JOYSTAT, READ16LE(&ioMem[COMM_JOYSTAT]) & ~JOYSTAT_SEND);
			UPDATE_REG(COMM_JOYCNT, READ16LE(&ioMem[COMM_JOYCNT]) | JOYCNT_SEND_COMPLETE);
			break;

		case JOY_CMD_WRITE:
			UPDATE_REG(COMM_JOY_RECV_L, (u16)((u16)data[2] << 8) | (u8)data[1]);
			UPDATE_REG(COMM_JOY_RECV_H, (u16)((u16)data[4] << 8) | (u8)data[3]);
			UPDATE_REG(COMM_JOYSTAT, READ16LE(&ioMem[COMM_JOYSTAT]) | JOYSTAT_RECV);
			UPDATE_REG(COMM_JOYCNT, READ16LE(&ioMem[COMM_JOYCNT]) | JOYCNT_RECV_COMPLETE);
			break;

		default:
			return; // ignore
		}

		resp.push_back((u8)READ16LE(&ioMem[COMM_JOYSTAT]));
		dol->Send(resp);

		// Generate SIO interrupt if we can
		if ( ((cmd == JOY_CMD_RESET) || (cmd == JOY_CMD_READ) || (cmd == JOY_CMD_WRITE))
			&& (READ16LE(&ioMem[COMM_JOYCNT]) & JOYCNT_INT_ENABLE) )
		{
			IF |= 0x80;
			UPDATE_REG(0x202, IF);
		}
	}
}

// Windows threading is within!
void LinkUpdate(int ticks)
{
	linktime += ticks;

	if (rfu_enabled)
	{
		linktime2 += ticks; // linktime2 is unused!
		rfu_transfer_end -= ticks;
		if (transfer && rfu_transfer_end <= 0) 
		{
			transfer = 0;
			if (READ16LE(&ioMem[COMM_SIOCNT]) & 0x4000)
			{
				IF |= 0x80;
				UPDATE_REG(0x202, IF);
			}
			UPDATE_REG(COMM_SIOCNT, READ16LE(&ioMem[COMM_SIOCNT]) & 0xff7f);
		}
		return;
	}

	if (lanlink.active)
	{
		if (lanlink.connected)
		{
			if (after)
			{
				if (linkid && linktime > 6044) {
					lc.Recv();
					oncewait = true;
				}
				else
					return;
			}

			if (linkid && !transfer && lc.numtransfers > 0 && linktime >= savedlinktime)
			{
				linkdata[linkid] = READ16LE(&ioMem[COMM_SIODATA8]);

				if (!lc.oncesend)
					lc.Send();

				lc.oncesend = false;
				UPDATE_REG(COMM_SIODATA32_L, linkdata[0]);
				UPDATE_REG(COMM_SIOCNT, READ16LE(&ioMem[COMM_SIOCNT]) | 0x80);
				transfer = 1;
				if (lc.numtransfers==1)
					linktime = 0;
				else
					linktime -= savedlinktime;
			}

			if (transfer && linktime >= trtimeend[lanlink.numgbas-1][tspeed])
			{
				if (READ16LE(&ioMem[COMM_SIOCNT]) & 0x4000)
				{
					IF |= 0x80;
					UPDATE_REG(0x202, IF);
				}

				UPDATE_REG(COMM_SIOCNT, (READ16LE(&ioMem[COMM_SIOCNT]) & 0xff0f) | (linkid << 4));
				transfer = 0;
				linktime -= trtimeend[lanlink.numgbas-1][tspeed];
				oncewait = false;

				if (!lanlink.speed)
				{
					if (linkid)
						lc.Recv();
					else
						ls.Recv(); // WTF is the point of this?

					UPDATE_REG(COMM_SIODATA32_H, linkdata[1]);
					UPDATE_REG(0x124, linkdata[2]);
					UPDATE_REG(0x126, linkdata[3]);
					oncewait = true;

				} else {

					after = true;
					if (lanlink.numgbas == 1)
					{
						UPDATE_REG(COMM_SIODATA32_H, linkdata[1]);
						UPDATE_REG(0x124, linkdata[2]);
						UPDATE_REG(0x126, linkdata[3]);
					}
				}
			}
		}
		return;
	}

	// ** CRASH ** linkmem is NULL, todo investigate why, added null check
	if (linkid && !transfer && linkmem && linktime >= linkmem->lastlinktime && linkmem->numtransfers)
	{
		linkmem->linkdata[linkid] = READ16LE(&ioMem[COMM_SIODATA8]);

		if (linkmem->numtransfers == 1)
		{
			linktime = 0;
#ifndef _XBOX
#ifndef _XBOX
			if (WaitForSingleObject(linksync[linkid], linktimeout) == WAIT_TIMEOUT)
				linkmem->numtransfers = 0;
#endif
#endif
		}
		else
			linktime -= linkmem->lastlinktime;

		switch ((linkmem->linkcmd[0]) >> 8)
		{
		case 'M':
			tspeed = (linkmem->linkcmd[0]) & 3;
			transfer = 1;
			WRITE32LE(&ioMem[COMM_SIODATA32_L], 0xffffffff);
			WRITE32LE(&ioMem[0x124], 0xffffffff);
			UPDATE_REG(COMM_SIOCNT, READ16LE(&ioMem[COMM_SIOCNT]) | 0x80);
			break;
		}
	}

	if (!transfer)
		return;

	if (transfer && linktime >= trtimedata[transfer-1][tspeed] && transfer <= linkmem->numgbas)
	{
		if (transfer-linkid == 2)
		{
#ifndef _XBOX
#ifndef _XBOX
			SetEvent(linksync[linkid+1]);
#endif
#ifndef _XBOX
			if (WaitForSingleObject(linksync[linkid], linktimeout) == WAIT_TIMEOUT)
				linkmem->numtransfers = 0;
#endif
#ifndef _XBOX
			ResetEvent(linksync[linkid]);
#endif
#endif
		}

		UPDATE_REG(0x11e + (transfer<<1), linkmem->linkdata[transfer-1]);
		transfer++;
	}

	if (transfer && linktime >= trtimeend[linkmem->numgbas-2][tspeed])
	{
		if (linkid == linkmem->numgbas-1)
		{
#ifndef _XBOX
#ifndef _XBOX
			SetEvent(linksync[0]);
#endif
#ifndef _XBOX
			if (WaitForSingleObject(linksync[linkid], linktimeout) == WAIT_TIMEOUT)
				linkmem->numtransfers = 0;
#endif

#ifndef _XBOX
			ResetEvent(linksync[linkid]);
#endif
#endif
			
		}

		transfer = 0;
		linktime -= trtimeend[0][tspeed];
		if (READ16LE(&ioMem[COMM_SIOCNT]) & 0x4000)
		{
			IF |= 0x80;
			UPDATE_REG(0x202, IF);
		}
		UPDATE_REG(COMM_SIOCNT, (READ16LE(&ioMem[COMM_SIOCNT]) & 0xff0f) | (linkid << 4));
		linkmem->linkdata[linkid] = 0xffff;
	}

	return;
}

inline int GetSIOMode(u16 siocnt, u16 rcnt)
{
	if (!(rcnt & 0x8000))
	{
		switch (siocnt & 0x3000) {
		case 0x0000: return NORMAL8;
		case 0x1000: return NORMAL32;
		case 0x2000: return MULTIPLAYER;
		case 0x3000: return UART;
		}
	}

	if (rcnt & 0x4000)
		return JOYBUS;

	return GP;
}

// The GBA wireless RFU (see adapter3.txt)
// Just try to avert your eyes for now ^^ (note, it currently can be called, tho)
u16 StartRFU(u16 value)
{
	switch (GetSIOMode(value, READ16LE(&ioMem[COMM_RCNT]))) {
	case NORMAL8:
		rfu_polarity = 0;
		return value;
		break;

	case NORMAL32:
		if (value & 8)
			value &= 0xfffb;	// A kind of acknowledge procedure
		else
			value |= 4;

		if (value & 0x80)
		{
			if ((value&3) == 1)
				rfu_transfer_end = 2048;
			else
				rfu_transfer_end = 256;

			u16 a = READ16LE(&ioMem[COMM_SIODATA32_H]);

			switch (rfu_state) {
			case RFU_INIT:
				if (READ32LE(&ioMem[COMM_SIODATA32_L]) == 0xb0bb8001)
					rfu_state = RFU_COMM;	// end of startup

				UPDATE_REG(COMM_SIODATA32_H, READ16LE(&ioMem[COMM_SIODATA32_L]));
				UPDATE_REG(COMM_SIODATA32_L, a);
				break;

			case RFU_COMM:
				if (a == 0x9966)
				{
					rfu_cmd = ioMem[COMM_SIODATA32_L];
					if ((rfu_qsend=ioMem[0x121]) != 0) {
						rfu_state = RFU_SEND;
						rfu_counter = 0;
					}
					if (rfu_cmd == 0x25 || rfu_cmd == 0x24) {
						linkmem->rfu_q[vbaid] = rfu_qsend;
					}
					UPDATE_REG(COMM_SIODATA32_L, 0);
					UPDATE_REG(COMM_SIODATA32_H, 0x8000);
				}
				else if (a == 0x8000)
				{
					switch (rfu_cmd) {
					case 0x1a:	// check if someone joined
						if (linkmem->rfu_request[vbaid] != 0) {
							rfu_state = RFU_RECV;
							rfu_qrecv = 1;
						}
						linkid = -1;
						rfu_cmd |= 0x80;
						break;

					case 0x1e:	// receive broadcast data
					case 0x1d:	// no visible difference
						rfu_polarity = 0;
						rfu_state = RFU_RECV;
						rfu_qrecv = 7;
						rfu_counter = 0;
						rfu_cmd |= 0x80;
						break;

					case 0x30:
						linkmem->rfu_request[vbaid] = 0;
						linkmem->rfu_q[vbaid] = 0;
						linkid = 0;
						numtransfers = 0;
						rfu_cmd |= 0x80;
						if (linkmem->numgbas == 2)
#ifndef _XBOX
							SetEvent(linksync[1-vbaid]);
#endif
						break;

					case 0x11:	// ? always receives 0xff - I suspect it's something for 3+ players
					case 0x13:	// unknown
					case 0x20:	// this has something to do with 0x1f
					case 0x21:	// this too
						rfu_cmd |= 0x80;
						rfu_polarity = 0;
						rfu_state = 3;
						rfu_qrecv = 1;
						break;

					case 0x26:
						if(linkid>0){
							rfu_qrecv = rfu_masterq;
						}
						if((rfu_qrecv=linkmem->rfu_q[1-vbaid])!=0){
							rfu_state = RFU_RECV;
							rfu_counter = 0;
						}
						rfu_cmd |= 0x80;
						break;

					case 0x24:	// send data
						if((numtransfers++)==0) linktime = 1;
						linkmem->rfu_linktime[vbaid] = linktime;
						if(linkmem->numgbas==2){
#ifndef _XBOX
							SetEvent(linksync[1-vbaid]);
#endif
#ifndef _XBOX
							WaitForSingleObject(linksync[vbaid], linktimeout);
#endif
							ResetEvent(linksync[vbaid]);
						}
						rfu_cmd |= 0x80;
						linktime = 0;
						linkid = -1;
						break;

					case 0x25:	// send & wait for data
					case 0x1f:	// pick a server
					case 0x10:	// init
					case 0x16:	// send broadcast data
					case 0x17:	// setup or something ?
					case 0x27:	// wait for data ?
					case 0x3d:	// init
					default:
						rfu_cmd |= 0x80;
						break;

					case 0xa5:	//	2nd part of send&wait function 0x25
					case 0xa7:	//	2nd part of wait function 0x27
						if (linkid == -1) {
							linkid++;
							linkmem->rfu_linktime[vbaid] = 0;
						}
						if (linkid&&linkmem->rfu_request[1-vbaid] == 0) {
							linkmem->rfu_q[1-vbaid] = 0;
							rfu_transfer_end = 256;
							rfu_polarity = 1;
							rfu_cmd = 0x29;
							linktime = 0;
							break;
						}
						if ((numtransfers++) == 0)
							linktime = 0;
						linkmem->rfu_linktime[vbaid] = linktime;
						if (linkmem->numgbas == 2) {
							if (!linkid || (linkid && numtransfers))
	#ifndef _XBOX
							SetEvent(linksync[1-vbaid]);
#endif
#ifndef _XBOX
							WaitForSingleObject(linksync[vbaid], linktimeout);
#endif
							ResetEvent(linksync[vbaid]);
						}
						if ( linkid > 0) {
							memcpy(rfu_masterdata, linkmem->rfu_data[1-vbaid], 128);
							rfu_masterq = linkmem->rfu_q[1-vbaid];
						}
						rfu_transfer_end = linkmem->rfu_linktime[1-vbaid] - linktime + 256;
						
						if (rfu_transfer_end < 256)
							rfu_transfer_end = 256;

						linktime = -rfu_transfer_end;
						rfu_polarity = 1;
						rfu_cmd = 0x28;
						break;
					}
					UPDATE_REG(COMM_SIODATA32_H, 0x9966);
					UPDATE_REG(COMM_SIODATA32_L, (rfu_qrecv<<8) | rfu_cmd);

				} else {

					UPDATE_REG(COMM_SIODATA32_L, 0);
					UPDATE_REG(COMM_SIODATA32_H, 0x8000);
				}
				break;

			case RFU_SEND:
				if(--rfu_qsend == 0)
					rfu_state = RFU_COMM;

				switch (rfu_cmd) {
				case 0x16:
					linkmem->rfu_bdata[vbaid][rfu_counter++] = READ32LE(&ioMem[COMM_SIODATA32_L]);
					break;

				case 0x17:
					linkid = 1;
					break;

				case 0x1f:
					linkmem->rfu_request[1-vbaid] = 1;
					break;

				case 0x24:
				case 0x25:
					linkmem->rfu_data[vbaid][rfu_counter++] = READ32LE(&ioMem[COMM_SIODATA32_L]);
					break;
				}
				UPDATE_REG(COMM_SIODATA32_L, 0);
				UPDATE_REG(COMM_SIODATA32_H, 0x8000);
				break;

			case RFU_RECV:
				if (--rfu_qrecv == 0)
					rfu_state = RFU_COMM;

				switch (rfu_cmd) {
				case 0x9d:
				case 0x9e:
					if (rfu_counter == 0) {
						UPDATE_REG(COMM_SIODATA32_L, 0x61f1);
						UPDATE_REG(COMM_SIODATA32_H, 0);
						rfu_counter++;
						break;
					}
					UPDATE_REG(COMM_SIODATA32_L, linkmem->rfu_bdata[1-vbaid][rfu_counter-1]&0xffff);
					UPDATE_REG(COMM_SIODATA32_H, linkmem->rfu_bdata[1-vbaid][rfu_counter-1]>>16);
					rfu_counter++;
					break;

			case 0xa6:
				if (linkid>0) {
					UPDATE_REG(COMM_SIODATA32_L, rfu_masterdata[rfu_counter]&0xffff);
					UPDATE_REG(COMM_SIODATA32_H, rfu_masterdata[rfu_counter++]>>16);
				} else {
					UPDATE_REG(COMM_SIODATA32_L, linkmem->rfu_data[1-vbaid][rfu_counter]&0xffff);
					UPDATE_REG(COMM_SIODATA32_H, linkmem->rfu_data[1-vbaid][rfu_counter++]>>16);
				}
				break;

			case 0x93:	// it seems like the game doesn't care about this value
				UPDATE_REG(COMM_SIODATA32_L, 0x1234);	// put anything in here
				UPDATE_REG(COMM_SIODATA32_H, 0x0200);	// also here, but it should be 0200
				break;

			case 0xa0:
			case 0xa1:
				UPDATE_REG(COMM_SIODATA32_L, 0x641b);
				UPDATE_REG(COMM_SIODATA32_H, 0x0000);
				break;

			case 0x9a:
				UPDATE_REG(COMM_SIODATA32_L, 0x61f9);
				UPDATE_REG(COMM_SIODATA32_H, 0);
				break;

			case 0x91:
				UPDATE_REG(COMM_SIODATA32_L, 0x00ff);
				UPDATE_REG(COMM_SIODATA32_H, 0x0000);
				break;

			default:
				UPDATE_REG(COMM_SIODATA32_L, 0x0173);
				UPDATE_REG(COMM_SIODATA32_H, 0x0000);
				break;
			}
			break;
		}
		transfer = 1;
	}

	if (rfu_polarity)
		value ^= 4;	// sometimes it's the other way around

	default:
		return value;
	}
}

//////////////////////////////////////////////////////////////////////////
// Probably from here down needs to be replaced with SFML goodness :)

int InitLink()
{
	linkid = 0;
	lanlink.tcpsocket.Close();
	lanlink.connected = false;
	lanlink.terminate = false;
	lanlink.active = false;
	lanlink.type = 0;
	lanlink.numgbas = 0;
	lanlink.speed = false;
	lanlink.thread = NULL;

#ifdef _XBOX
	// Xbox 360: Use SFML sockets directly, no file mapping needed
	linkmem = new LINKDATA;
	memset(linkmem, 0, sizeof(LINKDATA));
	vbaid = 0;
	linkmem->numgbas = 1;
	linkmem->linkflags = 0;
	for(int j=0;j<4;j++){
		linksync[j] = NULL;
	}
	return 1;
#else
	if((mmf=CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LINKDATA), "VBA link memory"))==NULL){
#ifndef _XBOX
		MessageBox(NULL, "Error creating file mapping", "Error", MB_OK|MB_ICONEXCLAMATION);
#endif
		return 0;
	}
#endif

#ifndef _XBOX
	if(GetLastError() == ERROR_ALREADY_EXISTS)
		vbaid = 1;
	else
		vbaid = 0;

	if((linkmem=(LINKDATA *)MapViewOfFile(mmf, FILE_MAP_WRITE, 0, 0, sizeof(LINKDATA)))==NULL){
		lanlink.tcpsocket.Close();
		CloseHandle(mmf);
#ifndef _XBOX
		MessageBox(NULL, "Error mapping file", "Error", MB_OK|MB_ICONEXCLAMATION);
#endif
		return 0;
	}
#endif

#ifndef _XBOX
	if(linkmem->linkflags&LINK_PARENTLOST)
		vbaid = 0;

	if(vbaid==0){
		linkid = 0;
		if(linkmem->linkflags&LINK_PARENTLOST){
			linkmem->numgbas++;
			linkmem->linkflags &= ~LINK_PARENTLOST;
		}
		else
			linkmem->numgbas=1;

		for(i=0;i<4;i++){
			linkevent[15]=(char)i+'1';
			if((linksync[i]=CreateEvent(NULL, true, false, linkevent))==NULL){
				lanlink.tcpsocket.Close();
				UnmapViewOfFile(linkmem);
				CloseHandle(mmf);
				for(j=0;j<i;j++){
					CloseHandle(linksync[j]);
				}
				MessageBox(NULL, "Error opening event", "Error", MB_OK|MB_ICONEXCLAMATION);
				return 0;
			}
		}
	} else {
		vbaid=linkmem->numgbas;
		linkid = vbaid;
		linkmem->numgbas++;

		if(linkmem->numgbas>4){
			linkmem->numgbas=4;
			lanlink.tcpsocket.Close();
			MessageBox(NULL, "5 or more GBAs not supported.", "Error!", MB_OK|MB_ICONEXCLAMATION);
			UnmapViewOfFile(linkmem);
			CloseHandle(mmf);
			return 0;
		}
		for(i=0;i<4;i++){
			linkevent[15]=(char)i+'1';
			if((linksync[i]=OpenEvent(EVENT_ALL_ACCESS, false, linkevent))==NULL){
				lanlink.tcpsocket.Close();
				CloseHandle(mmf);
				UnmapViewOfFile(linkmem);
				for(j=0;j<i;j++){
					CloseHandle(linksync[j]);
				}
				MessageBox(NULL, "Error opening event", "Error", MB_OK|MB_ICONEXCLAMATION);
				return 0;
			}
		}
	}
#else
	// Xbox: Initialize linkmem structure
	linkmem->numgbas = 1;
	linkmem->linkflags = 0;
	// Xbox doesn't use Windows events - will use SFML networking
	for(i=0;i<4;i++){
		linksync[i] = NULL;
	}
#endif

	linkmem->lastlinktime=0xffffffff;
	linkmem->numtransfers=0;
	linkmem->linkflags=0;
	lanlink.connected = false;
	lanlink.thread = NULL;
	lanlink.speed = false;
	for(i=0;i<4;i++){
		linkmem->linkdata[i] = 0xffff;
		linkdata[i] = 0xffff;
	}
	return 1;
}

void CloseLink(void){
	if(lanlink.connected){
		if(linkid){
			char outbuffer[4];
			outbuffer[0] = 4;
			outbuffer[1] = -32;
			if(lanlink.type==0) lanlink.tcpsocket.Send(outbuffer, 4);
		} else {
			char outbuffer[12];
			int i;
			outbuffer[0] = 12;
			outbuffer[1] = -32;
			for(i=1;i<=lanlink.numgbas;i++){
				if(lanlink.type==0){
					ls.tcpsocket[i].Send(outbuffer, 12);
				}
				ls.tcpsocket[i].Close();
			}
		}
	}
#ifndef _XBOX
	linkmem->numgbas--;
	if(!linkid&&linkmem->numgbas!=0)
		linkmem->linkflags|=LINK_PARENTLOST;
	CloseHandle(mmf);
	UnmapViewOfFile(linkmem);

	for(i=0;i<4;i++){
		if(linksync[i]!=NULL){
			PulseEvent(linksync[i]);
			CloseHandle(linksync[i]);
		}
	}
	regSetDwordValue("LAN", lanlink.active);
#else
	// Xbox: Clean up allocated memory
	if(linkmem){
		delete linkmem;
		linkmem = NULL;
	}
#endif
	lanlink.tcpsocket.Close();
	return;
}

// Server
lserver::lserver(void){
	intinbuffer = (int*)inbuffer;
	u16inbuffer = (u16*)inbuffer;
	intoutbuffer = (int*)outbuffer;
	u16outbuffer = (u16*)outbuffer;
	oncewait = false;
	howmanytimes = 0;
}

int lserver::Init(void *serverdlg){
#ifdef _XBOX
	// Xbox: No Windows threading, will handle in main loop
	lanlink.tcpsocket.Close();
	if(lanlink.tcpsocket.Listen(5738) != sf::Socket::Done)
		return -1;
	lanlink.tcpsocket.SetBlocking(false);
	lanlink.terminate = false;
	linkid = 0;
	return 0;
#else
	DWORD nothing;
	lanlink.tcpsocket.Close();
	if(lanlink.tcpsocket.Listen(5738) != sf::Socket::Done)
		return -1;
	lanlink.tcpsocket.SetBlocking(false);

	if(lanlink.thread!=NULL){
		lanlink.terminate = true;
#ifndef _XBOX
		WaitForSingleObject(linksync[vbaid], 500);
#endif
		lanlink.thread = NULL;
	}
	lanlink.terminate = false;
	linkid = 0;

	sf::IPAddress localaddr = sf::IPAddress::GetLocalAddress();
	((ServerWait*)serverdlg)->m_serveraddress.Format("Server IP address is: %s", localaddr.ToString().c_str());

	lanlink.thread = CreateThread(NULL, 0, LinkServerThread, serverdlg, 0, &nothing);

	return 0;
#endif
}

#ifndef _XBOX
DWORD WINAPI LinkServerThread(void *serverdlg){
	char inbuffer[256], outbuffer[256];
	int *intinbuffer = (int*)inbuffer;
	u16 *u16inbuffer = (u16*)inbuffer;
	int *intoutbuffer = (int*)outbuffer;
	u16 *u16outbuffer = (u16*)outbuffer;
	i = 0;

	while(i<lanlink.numgbas){
		sf::SocketTCP incoming;
		sf::Socket::Status status = lanlink.tcpsocket.Accept(incoming);
		if(status == sf::Socket::Done){
			incoming.SetBlocking(false);
			ls.tcpsocket[i+1] = incoming;
			u16outbuffer[0] = i+1;
			u16outbuffer[1] = lanlink.numgbas;
			SendExact(ls.tcpsocket[i+1], outbuffer, 4);
#ifndef _XBOX
			((ServerWait*)serverdlg)->m_plconn[i].Format("Player %d connected", i+1);
			((ServerWait*)serverdlg)->UpdateData(false);
#endif
			i++;
		}else{
			if(lanlink.terminate){
#ifndef _XBOX
				SetEvent(linksync[vbaid]);
#endif
				return 0;
			}
#ifndef _XBOX
			Sleep(10);
#else
			// Xbox: Use SFML timing or yield
#endif
		}
#ifndef _XBOX
		((ServerWait*)serverdlg)->m_prgctrl.StepIt();
#endif
	}
#ifndef _XBOX
	MessageBox(NULL, "All players connected", "Link", MB_OK);
	((ServerWait*)serverdlg)->SendMessage(WM_CLOSE, 0, 0);
#endif

	for(i=1;i<=lanlink.numgbas;i++){
		outbuffer[0] = 4;
		SendExact(ls.tcpsocket[i], outbuffer, 4);
	}

	lanlink.connected = true;

	return 0;
}
#endif // _XBOX  // closes the #ifndef _XBOX before LinkServerThread

void lserver::Send(void){
	if(lanlink.type==0){	// TCP
		if(savedlinktime==-1){
			outbuffer[0] = 4;
			outbuffer[1] = -32;	//0xe0
			for(i=1;i<=lanlink.numgbas;i++){
				SendExact(tcpsocket[i], outbuffer, 4);
				ReceiveExact(tcpsocket[i], inbuffer, 4);
			}
		}
		outbuffer[1] = tspeed;
		u16outbuffer[1] = linkdata[0];
		intoutbuffer[1] = savedlinktime;
		if(lanlink.numgbas==1){
			if(lanlink.type==0){
				outbuffer[0] = 8;
				SendExact(tcpsocket[1], outbuffer, 8);
			}
		}
		else if(lanlink.numgbas==2){
			u16outbuffer[4] = linkdata[2];
			if(lanlink.type==0){
				outbuffer[0] = 10;
				SendExact(tcpsocket[1], outbuffer, 10);
				u16outbuffer[4] = linkdata[1];
				SendExact(tcpsocket[2], outbuffer, 10);
			}
		} else {
			if(lanlink.type==0){
				outbuffer[0] = 12;
				u16outbuffer[4] = linkdata[2];
				u16outbuffer[5] = linkdata[3];
				SendExact(tcpsocket[1], outbuffer, 12);
				u16outbuffer[4] = linkdata[1];
				SendExact(tcpsocket[2], outbuffer, 12);
				u16outbuffer[5] = linkdata[2];
				SendExact(tcpsocket[3], outbuffer, 12);
			}
		}
	}
	return;
}

void lserver::Recv(void){
	if(lanlink.type==0){	// TCP
		for(i=0;i<lanlink.numgbas;i++){
			std::size_t received = 0;
			sf::Socket::Status status = tcpsocket[i+1].Receive(inbuffer, 1, received);
			if(status != sf::Socket::Done || received == 0)
				continue;

			int expected = (unsigned char)inbuffer[0];
			if(expected <= 1 || expected > 255)
				continue;

			if(!ReceiveExact(tcpsocket[i+1], inbuffer+1, expected-1))
				continue;

			if(inbuffer[1]==-32){
				char message[30];
				lanlink.connected = false;
#ifndef _XBOX
				sprintf(message, "Player %d disconnected.", i+2);
				MessageBox(NULL, message, "Link", MB_OK);
#endif
				outbuffer[0] = 4;
				outbuffer[1] = -32;
				for(int j=1;j<lanlink.numgbas;j++){
					SendExact(tcpsocket[j], outbuffer, 12);
					ReceiveExact(tcpsocket[j], inbuffer, 256);
					tcpsocket[j].Close();
				}
				return;
			}
			linkdata[i+1] = u16inbuffer[1];
		}
	}
	after = false;
	return;
}

// Client
lclient::lclient(void){
	intinbuffer = (int*)inbuffer;
	u16inbuffer = (u16*)inbuffer;
	intoutbuffer = (int*)outbuffer;
	u16outbuffer = (u16*)outbuffer;
	numtransfers = 0;
	oncesend = false;
	numbytes = 0;
	return;
}

int lclient::Init(sf::IPAddress hostaddr, void *waitdlg){
#ifdef _XBOX
	// Xbox: No Windows threading, will handle in main loop
	if(hostaddr.IsValid())
		serveraddr = hostaddr;
	else
		serveraddr = sf::IPAddress::LocalHost;
	
	lanlink.terminate = false;
	lanlink.tcpsocket.Close();
	if(lanlink.tcpsocket.Connect(5738, serveraddr) != sf::Socket::Done)
		return 1;
	lanlink.tcpsocket.SetBlocking(false);
	
	char inbuffer[16];
	u16 *u16inbuffer = (u16*)inbuffer;
	if(!ReceiveExact(lanlink.tcpsocket, inbuffer, 4))
		return 1;
	linkid = (int)u16inbuffer[0];
	lanlink.numgbas = (int)u16inbuffer[1];
	
	if(!ReceiveExact(lanlink.tcpsocket, inbuffer, 4))
		return 1;
	
	lanlink.connected = true;
	return 0;
#else
	DWORD nothing;

	if(hostaddr.IsValid())
		serveraddr = hostaddr;
	else
		serveraddr = sf::IPAddress::LocalHost;

	if(lanlink.thread!=NULL){
		lanlink.terminate = true;
		WaitForSingleObject(linksync[vbaid], 500);
		lanlink.thread = NULL;
	}

	((ServerWait*)waitdlg)->SetWindowText("Connecting...");
	lanlink.terminate = false;
	lanlink.thread = CreateThread(NULL, 0, LinkClientThread, waitdlg, 0, &nothing);
	return 0;
#endif
}

#ifndef _XBOX
DWORD WINAPI LinkClientThread(void *waitdlg){
	char inbuffer[16];
	u16 *u16inbuffer = (u16*)inbuffer;
	lanlink.tcpsocket.Close();
	if(lanlink.tcpsocket.Connect(5738, lc.serveraddr) != sf::Socket::Done){
#ifndef _XBOX
		MessageBox(NULL, "Couldn't connect to server.", "Link", MB_OK);
#endif
		return 1;
	}

	lanlink.tcpsocket.SetBlocking(false);

	if(!ReceiveExact(lanlink.tcpsocket, inbuffer, 4)){
#ifndef _XBOX
		MessageBox(NULL, "Handshake failed.", "Link", MB_OK);
#endif
		return 1;
	}
	linkid = (int)u16inbuffer[0];
	lanlink.numgbas = (int)u16inbuffer[1];

#ifndef _XBOX
	((ServerWait*)waitdlg)->m_serveraddress.Format("Connected as #%d", linkid+1);
	if(lanlink.numgbas!=linkid)	((ServerWait*)waitdlg)->m_plconn[0].Format("Waiting for %d players to join", lanlink.numgbas-linkid);
	else ((ServerWait*)waitdlg)->m_plconn[0].Format("All players joined.");
#endif

	if(!ReceiveExact(lanlink.tcpsocket, inbuffer, 4)){
#ifndef _XBOX
		MessageBox(NULL, "Server handshake failed.", "Link", MB_OK);
#endif
		return 1;
	}

#ifndef _XBOX
	MessageBox(NULL, "Connected.", "Link", MB_OK);
#endif
#ifndef _XBOX
	((ServerWait*)waitdlg)->SendMessage(WM_CLOSE, 0, 0);
#endif

	lanlink.connected = true;
	return 0;
}
#endif

void lclient::CheckConn(void){
	std::size_t received = 0;
	sf::Socket::Status status = lanlink.tcpsocket.Receive(inbuffer, 1, received);
	if(status != sf::Socket::Done || received == 0)
		return;

	int expected = (unsigned char)inbuffer[0];
	if(expected <= 1 || expected > 255)
		return;

	if(!ReceiveExact(lanlink.tcpsocket, inbuffer+1, expected-1))
		return;

	if(inbuffer[1]==-32){
		outbuffer[0] = 4;
		SendExact(lanlink.tcpsocket, outbuffer, 4);
		lanlink.connected = false;
#ifndef _XBOX
		MessageBox(NULL, "Server disconnected.", "Link", MB_OK);
#endif
		return;
	}
	numtransfers = 1;
	savedlinktime = 0;
	linkdata[0] = u16inbuffer[1];
	tspeed = inbuffer[1] & 3;
	for(i=1, numbytes=4;i<=lanlink.numgbas;i++)
		if(i!=linkid) linkdata[i] = u16inbuffer[numbytes++];
	after = false;
	oncewait = true;
	oncesend = true;
	return;
}

void lclient::Recv(void){
	std::size_t received = 0;
	sf::Socket::Status status = lanlink.tcpsocket.Receive(inbuffer, 1, received);
	if(status != sf::Socket::Done || received == 0){
		numtransfers = 0;
		return;
	}

	int expected = (unsigned char)inbuffer[0];
	if(expected <= 1 || expected > 255){
		numtransfers = 0;
		return;
	}

	if(!ReceiveExact(lanlink.tcpsocket, inbuffer+1, expected-1)){
		numtransfers = 0;
		return;
	}

	if(inbuffer[1]==-32){
		outbuffer[0] = 4;
		SendExact(lanlink.tcpsocket, outbuffer, 4);
		lanlink.connected = false;
#ifndef _XBOX
		MessageBox(NULL, "Server disconnected.", "Link", MB_OK);
#endif
		return;
	}
	tspeed = inbuffer[1] & 3;
	linkdata[0] = u16inbuffer[1];
	savedlinktime = intinbuffer[1];
	for(i=1, numbytes=4;i<lanlink.numgbas+1;i++)
		if(i!=linkid) linkdata[i] = u16inbuffer[numbytes++];
	numtransfers++;
	if(numtransfers==0) numtransfers = 2;
	after = false;
}

void lclient::Send(){
	outbuffer[0] = 4;
	outbuffer[1] = linkid<<2;
	u16outbuffer[1] = linkdata[linkid];
	SendExact(lanlink.tcpsocket, outbuffer, 4);
	return;
}


// Uncalled
void LinkSStop(void){
	if(!oncewait){
		if(linkid){
			if(lanlink.numgbas==1) return;
			lc.Recv();
		}
		else ls.Recv();

		oncewait = true;
		UPDATE_REG(COMM_SIODATA32_H, linkdata[1]);
		UPDATE_REG(0x124, linkdata[2]);
		UPDATE_REG(0x126, linkdata[3]);
	}
	return;
}

// ??? Called when COMM_SIODATA8 written
void LinkSSend(u16 value){
	if(linkid&&!lc.oncesend){
		linkdata[linkid] = value;
		lc.Send();
		lc.oncesend = true;
	}
}