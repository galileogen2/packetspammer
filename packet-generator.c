/**
 * Hello, and welcome to this brief, but hopefully complete, example file for
 * wireless packet injection using pcap.
 *
 * Although there are various resources for this spread on the web, it is hard
 * to find a single, cohesive piece that shows how everything fits together.
 * This file aims to give such an example, constructing a fully valid UDP packet
 * all the way from the 802.11 PHY header (through radiotap) to the data part of
 * the packet and then injecting it on a wireless interface
 *
 * Skip down a couple of lines, as the following is just headers and such that
 * we need.
 */
#include <pcap.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <unistd.h>
#include <time.h>

/* Defined in include/linux/ieee80211.h */
struct ieee80211_hdr {
  uint16_t /*__le16*/ frame_control;
  uint16_t /*__le16*/ duration_id;
  uint8_t addr1[6];
  uint8_t addr2[6];
  uint8_t addr3[6];
  uint16_t /*__le16*/ seq_ctrl;
  //uint8_t addr4[6];
} __attribute__ ((packed));

#define WLAN_FC_TYPE_DATA	2
#define WLAN_FC_SUBTYPE_DATA	0


/*************************** START READING AGAIN ******************************/

/* A bogus MAC address just to show that it can be done */
const uint8_t mac[6] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab };

/**
 * Note that we are using the broadcast address as the destination and the
 * link-local address as the source to be nice to routers and such.
 *
 */
const char * to = "255.255.255.255";
const char * from = "169.254.1.1";

/**
 * Radiotap is a protocol of sorts that is used to convey information about the
 * physical-layer part of wireless transmissions. When monitoring an interface
 * for packets, it will contain information such as what rate was used, what
 * channel it was sent on, etc. When injecting a packet, we can use it to tell
 * the 802.11 card how we want the frame to be transmitted.
 *
 * The format of the radiotap header is somewhat odd.
 * include/net/ieee80211_radiotap.h does an okay job of explaining it, but I'll
 * try to give a quick overview here.
 *
 * Keep in mind that all the fields here are little-endian, so you should
 * reverse the order of the bytes in your head when reading. Also, fields that
 * are set to 0 just mean that we let the card choose what values to use for
 * that option (for rate and channel for example, we'll let the card decide).
 */
static const uint8_t u8aRadiotapHeader[] = {

  0x00, 0x00, // <-- radiotap version (ignore this)
  0x18, 0x00, // <-- number of bytes in our header (count the number of "0x"s)

  /**
   * The next field is a bitmap of which options we are including.
   * The full list of which field is which option is in ieee80211_radiotap.h,
   * but I've chosen to include:
   *   0x00 0x01: timestamp
   *   0x00 0x02: flags
   *   0x00 0x03: rate
   *   0x00 0x04: channel
   *   0x80 0x00: tx flags (seems silly to have this AND flags, but oh well)
   */
  0x0f, 0x80, 0x00, 0x00,

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // <-- timestamp

  /**
   * This is the first set of flags, and we've set the bit corresponding to
   * IEEE80211_RADIOTAP_F_FCS, meaning we want the card to add a FCS at the end
   * of our buffer for us.
   */
  0x10,

  0x00, // <-- rate
  0x00, 0x00, 0x00, 0x00, // <-- channel

  /**
   * This is the second set of flags, specifically related to transmissions. The
   * bit we've set is IEEE80211_RADIOTAP_F_TX_NOACK, which means the card won't
   * wait for an ACK for this frame, and that it won't retry if it doesn't get
   * one.
   */
  0x08, 0x00,
};

/**
 * After an 802.11 MAC-layer header, a logical link control (LLC) header should
 * be placed to tell the receiver what kind of data will follow (see IEEE 802.2
 * for more information).
 *
 * For political reasons, IP wasn't allocated a global so-called SAP number,
 * which means that a simple LLC header is not enough to indicate that an IP
 * frame was sent. 802.2 does, however, allow EtherType types (the same kind of
 * type numbers used in, you guessed it, Ethernet) through the use of the
 * "Subnetwork Access Protocol", or SNAP. To use SNAP, the three bytes in the
 * LLC have to be set to the magical numbers 0xAA 0xAA 0x03. The next five bytes
 * are then interpreted as a SNAP header. To specify an EtherType, we need to
 * set the first three of them to 0. The last two bytes can then finally be set
 * to 0x0800, which is the IP EtherType.
 */
const uint8_t ipllc[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00 };

/**
 * A simple implementation of the internet checksum used by IP
 * Not very interesting, so it has been moved below main()
 */
uint16_t inet_csum(const void *buf, size_t hdr_len);

int nDelay = 1000000;
char command1[50];
char command2[50];
char command3[50];
char command4[50];
char command5[50];

int main(void) {

strcpy(command1, "echo -n \"40\" > /sys/class/gpio/unexport");
strcpy(command2, "echo -n \"40\" > /sys/class/gpio/export");
strcpy(command3, "echo -n \"out\" > /sys/class/gpio/gpio40/direction");
strcpy(command4, "echo -n \"1\" > /sys/class/gpio/gpio40/value");
strcpy(command5, "echo -n \"0\" > /sys/class/gpio/gpio40/value");
system(command1);
system(command2);
system(command3);

system(command4);
usleep(nDelay);
usleep(nDelay);
usleep(nDelay);
usleep(nDelay);
usleep(nDelay);
usleep(nDelay);
system(command5);

struct timespec send_time;
struct timespec start_time;
struct timespec end_time;
long diffInNanos;
int packno = 0;
char buff[100];

while (1)
{

  clock_gettime(CLOCK_REALTIME, &send_time);
  strftime(buff, sizeof buff, "%D %T", gmtime(&send_time.tv_sec));

  /* The parts of our packet */
  uint8_t *rt; /* radiotap */
  struct ieee80211_hdr *hdr;
  uint8_t *llc;
  struct iphdr *ip;
  struct udphdr *udp;
  uint8_t *data;
  uint8_t *stime;

  /* Other useful bits */
  uint8_t *buf;
  size_t sz;
  uint8_t fcchunk[2]; /* 802.11 header frame control */
  struct sockaddr_in saddr, daddr; /* IP source and destination */

  /* PCAP vars */
  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_t *ppcap;
  
  /* Total buffer size (note the 0 bytes of data and the 4 bytes of FCS */
  sz = sizeof(u8aRadiotapHeader) + sizeof(struct ieee80211_hdr) + sizeof(ipllc) + sizeof(struct iphdr) + sizeof(struct udphdr) + /*0*/ sizeof(struct timespec) + 100 /* data */ + 4 /* FCS */;
  buf = (uint8_t *) malloc(sz);

  /* Put our pointers in the right place */
  rt = (uint8_t *) buf;
  hdr = (struct ieee80211_hdr *) (rt+sizeof(u8aRadiotapHeader));
  llc = (uint8_t *) (hdr+1);
  ip = (struct iphdr *) (llc+sizeof(ipllc));
  udp = (struct udphdr *) (ip+1);
  data = (uint8_t *) (udp+1);

  // copy time in data
  memcpy(data, &send_time, sizeof(send_time));
  //  string time
  stime = (struct timespec *) (data+1);
  memcpy(stime, buff, 100);

  /* The radiotap header has been explained already */
  memcpy(rt, u8aRadiotapHeader, sizeof(u8aRadiotapHeader));

  /**
   * Next, we need to construct the 802.11 header
   *
   * The biggest trick here is the frame control field.
   * http://www.wildpackets.com/resources/compendium/wireless_lan/wlan_packets
   * gives a fairly good explanation.
   *
   * The first byte of the FC gives the type and "subtype" of the 802.11 frame.
   * We're transmitting a data frame, so we set both the type and the subtype to
   * DATA.
   *
   * Most guides also forget to mention that the bits *within each byte* in the
   * FC are reversed (!!!), so FROMDS is actually the *second to last* bit in
   * the FC, hence 0x02.
   */
  fcchunk[0] = ((WLAN_FC_TYPE_DATA << 2) | (WLAN_FC_SUBTYPE_DATA << 4));
  fcchunk[1] = 0x02;
  memcpy(&hdr->frame_control, &fcchunk[0], 2*sizeof(uint8_t));

  /**
   * The remaining fields are more straight forward.
   * The duration we can set to some arbitrary high number, and the sequence
   * number can safely be set to 0.
   * The addresses here can be set to whatever, but bear in mind that which
   * address corresponds to source/destination/BSSID will vary depending on
   * which of TODS and FROMDS are set. The full table can be found at the
   * wildpackets.com link above, or condensed here:
   *
   *  +-------+---------+-------------+-------------+-------------+-----------+
   *  | To DS | From DS | Address 1   | Address 2   | Address 3   | Address 4 |
   *  +-------+---------+-------------+-------------+-------------+-----------+
   *  |     0 |       0 | Destination | Source      | BSSID       | N/A       |
   *  |     0 |       1 | Destination | BSSID       | Source      | N/A       |
   *  |     1 |       0 | BSSID       | Source      | Destination | N/A       |
   *  |     1 |       1 | Receiver    | Transmitter | Destination | Source    |
   *  +-------+---------+-------------+-------------+-------------+-----------+
   *
   * Also note that addr4 has been commented out. This is because it should not
   * be present unless both TODS *and* FROMDS has been set (as shown above).
   */
  hdr->duration_id = 0xffff;
  memcpy(&hdr->addr1[0], mac, 6*sizeof(uint8_t));
  memcpy(&hdr->addr2[0], mac, 6*sizeof(uint8_t));
  memcpy(&hdr->addr3[0], mac, 6*sizeof(uint8_t));
  hdr->seq_ctrl = 0;
  //hdr->addr4;

  /* The LLC+SNAP header has already been explained above */
  memcpy(llc, ipllc, 8*sizeof(uint8_t));

  /**
   * Now we're getting into familiar territory, IP headers!
   *
   * Remember that the tot_length is little-endian, so we need to run htons()
   * over the entire "real" length.
   */
  daddr.sin_family = AF_INET;
  saddr.sin_family = AF_INET;
  daddr.sin_port = htons(50505);
  saddr.sin_port = htons(50505);
  inet_pton(AF_INET, to, (struct in_addr *)&daddr.sin_addr.s_addr);
  inet_pton(AF_INET, from, (struct in_addr *)&saddr.sin_addr.s_addr);

  ip->ihl      = 5; /* header length, number of 32-bit words */
  ip->version  = 4;
  ip->tos      = 0x0;
  ip->id       = 0;
  ip->frag_off = htons(0x4000); /* Don't fragment */
  ip->ttl      = 64;
  ip->tot_len  = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + 0 /* data */);
  ip->protocol = IPPROTO_UDP;
  ip->saddr    = saddr.sin_addr.s_addr;
  ip->daddr    = daddr.sin_addr.s_addr;

  /**
   * The checksum should be calculated over the entire header with the checksum
   * field set to 0, so that's what we do
   */
  ip->check    = 0; 
  ip->check    = inet_csum(ip, sizeof(struct iphdr));

  /**
   * The UDP header is refreshingly simple.
   * Again, notice the little-endianness of ->len
   * UDP also lets us set the checksum to 0 to ignore it
   */
  udp->source  = saddr.sin_port;
  udp->dest    = daddr.sin_port;
  udp->len     = htons(sizeof(struct udphdr) + 0 /* data */);
  udp->check   = 0;

  /**
   * Finally, we have the packet and are ready to inject it.
   * First, we open the interface we want to inject on using pcap.
   */
  ppcap = pcap_open_live("mon0", 800, 1, 20, errbuf);

  if (ppcap == NULL) {
    printf("Could not open interface mon0 for packet injection: %s", errbuf);
    return 2;
  }

  /**
   * Then we send the packet and clean up after ourselves
   */
  system(command4);
  clock_gettime(CLOCK_REALTIME, &start_time);
  if (pcap_sendpacket(ppcap, buf, sz) == 0) {
    clock_gettime(CLOCK_REALTIME, &end_time);
    packno++;
    //system(command5);
    diffInNanos = end_time.tv_nsec - start_time.tv_nsec;
    strftime(buff, sizeof buff, "%D %T", gmtime(&end_time.tv_sec));
    printf("Send a packet [%d] at %s.%09ld with %ld ns \n\n",packno, buff, end_time.tv_nsec, diffInNanos);
    pcap_close(ppcap);
    system(command5);

                if ( packno >= 20 )
                {
                        return 0;
                }

    //return 0;
  }
  else {
    pcap_perror(ppcap, "Failed to inject packet");
    pcap_close(ppcap);
  }

  /**
   * If something went wrong, let's let our user know
   */
  //pcap_perror(ppcap, "Failed to inject packet");
  //pcap_close(ppcap);
  if (nDelay)
    usleep(nDelay);
}
  return 1;
}

/**
 * And that's it - a complete wireless packet injection function using pcap!
 */

uint16_t inet_csum(const void *buf, size_t hdr_len)
{
  unsigned long sum = 0;
  const uint16_t *ip1;

  ip1 = (const uint16_t *) buf;
  while (hdr_len > 1)
  {
    sum += *ip1++;
    if (sum & 0x80000000)
      sum = (sum & 0xFFFF) + (sum >> 16);
    hdr_len -= 2;
  }

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);

  return(~sum);
}
