/*
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation,  either version 3 of the License,  or (at your option)  any later
 * version.
 *
 * This program is  distributed in the hope that it will be useful,  but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) Mateusz Viste 2013. All rights reserved.
 * Contact: <mateusz$viste-family.net> (replace the $ sign by a @)
 */

#include "dhcp.c"

#include "processpkt.h"  /* include self for control */

static uint32_t iparr2uint(uint8_t *iparr) {
  uint32_t res;
  res = iparr[0];
  res <<= 8;
  res |= iparr[1];
  res <<= 8;
  res |= iparr[2];
  res <<= 8;
  res |= iparr[3];
  return(res);
}



/* processes the received frame */
int processpkt(uint8_t *buff, int bufflen, uint32_t *myip, uint32_t mynetmask, uint8_t *mymac, int slirpfd, struct arptabletype **arptable, int tapfd) {
    uint8_t *srcmac, *dstmac;
    int vlanid, ethertype;
    uint8_t *ethpayload, *ippayload, *udppayload;
    uint8_t *ipsrc = NULL, *ipdst = NULL;
    int x;
    uint8_t broadcastmac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int ipprotocol, fragoffset = 0, morefragsflag = 0, ipdscp, ipttl, ipid, portsrc = 0, portdst = 0;
    ethpayload = parse_ethernet(buff, bufflen, &srcmac, &dstmac, &vlanid, &ethertype);
    /* printf("Got frame from %02X:%02X:%02X:%02X:%02X:%02X to %02X:%02X:%02X:%02X:%02X:%02X with ethtype 0x%02X.\n", srcmac[0], srcmac[1], srcmac[2], srcmac[3], srcmac[4], srcmac[5], dstmac[0], dstmac[1], dstmac[2], dstmac[3], dstmac[4], dstmac[5], ethertype); */

    /* check the dst mac - if it's not for us, forget about the frame */
    if ((maccmp(mymac, dstmac) != 0) && (maccmp(broadcastmac, dstmac) != 0)) return(0);

    if (ethertype == 0x806) { /* ARP */
        uint8_t arpreqhdr[] = {0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01};
        int arpreqvalidated = 1;
        /* check that this is an ARP request */
        for (x = 0; x < 4; x++) if (dstmac[x] != 0xFF) arpreqvalidated = 0;
        for (x = 0; (x < 8) && (arpreqvalidated != 0); x++) {
          if (ethpayload[x] != arpreqhdr[x]) arpreqvalidated = 0;
        }
        if (arpreqhdr != 0) {
          uint32_t localip = iparr2uint(&ethpayload[24]);
          arpreqvalidated = 0; /* check that the IP that the host asks about is one of ours */
          for (x = 0; myip[x] != 0; x++) {
            if (localip == myip[x]) arpreqvalidated = 1;
          }
          if (arpreqvalidated != 0) { /* we will answer only for our own IP */
            uint8_t remotehostmac[6];
            uint8_t remotehostip[4];
            /* copy the mac and IP of the asking host into a safe place */
            /* printf("copy stuff into safe place (ethpayload = %p)", ethpayload); */
            for (x = 0; x < 6; x++) remotehostmac[x] = srcmac[x];
            for (x = 0; x < 4; x++) remotehostip[x] = ethpayload[8 + x];
            /* puts("alles klar"); */
            /* all is clear - we can send an ARP answer back */
            x = 0;
            buff[x++] = remotehostmac[0]; /* mac dst */
            buff[x++] = remotehostmac[1];
            buff[x++] = remotehostmac[2];
            buff[x++] = remotehostmac[3];
            buff[x++] = remotehostmac[4];
            buff[x++] = remotehostmac[5];
            buff[x++] = mymac[0];  /* mac src */
            buff[x++] = mymac[1];
            buff[x++] = mymac[2];
            buff[x++] = mymac[3];
            buff[x++] = mymac[4];
            buff[x++] = mymac[5];
            buff[x++] = 0x08;  /* ethertype is 'ARP' */
            buff[x++] = 0x06;
            buff[x++] = 0x00; /* hw type = ethernet */
            buff[x++] = 0x01;
            buff[x++] = 0x08; /* proto type = IP */
            buff[x++] = 0x00;
            buff[x++] = 0x06; /* hw size */
            buff[x++] = 0x04; /* proto size */
            buff[x++] = 0x00; /* opcode */
            buff[x++] = 0x02;
            buff[x++] = mymac[0]; /* sender mac addr */
            buff[x++] = mymac[1];
            buff[x++] = mymac[2];
            buff[x++] = mymac[3];
            buff[x++] = mymac[4];
            buff[x++] = mymac[5];
            buff[x++] = (localip >> 24) & 0xFF; /* sender IP addr */
            buff[x++] = (localip >> 16) & 0xFF;
            buff[x++] = (localip >> 8) & 0xFF;
            buff[x++] = localip & 0xFF;
            buff[x++] = remotehostmac[0]; /* target mac */
            buff[x++] = remotehostmac[1];
            buff[x++] = remotehostmac[2];
            buff[x++] = remotehostmac[3];
            buff[x++] = remotehostmac[4];
            buff[x++] = remotehostmac[5];
            buff[x++] = remotehostip[0];  /* target IP */
            buff[x++] = remotehostip[1];
            buff[x++] = remotehostip[2];
            buff[x++] = remotehostip[3];
            write(tapfd, buff, x); /* send the answer back */
            return(0);
          }
        }
      } else if (ethertype == 0x800) { /* IPv4 */
        ippayload = parse_ipv4(ethpayload, bufflen - (buff - ethpayload), &ipsrc, &ipdst, &ipprotocol, &ipdscp, &ipttl, &ipid, &fragoffset, &morefragsflag);
        *arptable = arp_learn(*arptable, srcmac, iparr2uint(ipsrc)); /* fill the arp table if necessary */
        if ((fragoffset == 0) && (morefragsflag == 0)) {  /* ignore all fragmentation */
          /* printf("packet is from %u.%u.%u.%u to %u.%u.%u.%u, ipproto = %d\n", ipsrc[0], ipsrc[1], ipsrc[2], ipsrc[3], ipdst[0], ipdst[1], ipdst[2], ipdst[3], ipprotocol); */
          if (ipprotocol == 17) {  /* UDP */
            udppayload = parse_udp(ippayload, bufflen - (buff - ippayload), &portsrc, &portdst);
            /* printf("UDP dgram from port %d to port %d\n", portsrc, portdst); */
            if ((portsrc == 68) && (portdst == 67)) {  /* I only care if it's a DHCP request */
              uint8_t *buffptr = buff + 128;
              /* puts("Got a DHCP request"); */
              x = dhcpserver(udppayload, bufflen - (buff - udppayload), arptable, myip[0], mynetmask, mymac, &buffptr);
              if (x > 0) {
                write(tapfd, buffptr, x); /* send the answer back */
                return(0);
              }
            }
          }
        }
        /* finally, send the packet to SLIRP (if it's not a broadcast packet) */
        if ((ipdst[0] != 0xFF) && (ipdst[1] != 0xFF) && (ipdst[2] != 0xFF) && (ipdst[3] != 0xFF)) {
          slirp_send(ethpayload, bufflen - (buff - ethpayload), slirpfd);
        }
    }
  return(0);
}
