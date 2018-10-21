/*
 * gen_uuid.c --- generate a DCE-compatible uuid
 *
 * Copyright (C) 1996, 1997, 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

/*
 * Force inclusion of SVID stuff since we need it if we're compiling in
 * gcc-wall wall mode
 */
#define _SVID_SOURCE

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NET_IF_DL_H
#include <net/if_dl.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "uuidP.h"

#ifdef HAVE_SRANDOM
#define srand(x) 	srandom(x)
#define rand() 		random()
#endif

#if defined(__linux__) && defined(__NR_gettid) && defined(HAVE_JRAND48)
#define DO_JRAND_MIX
static unsigned short jrand_seed[3];
#endif

static int get_random_fd(void)
{
	struct timeval	tv;
	static int	fd = -2;
	int		i;

	if (fd == -2) {
		gettimeofday(&tv, 0);
		fd = open("/dev/urandom", O_RDONLY);
		if (fd == -1)
			fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
		if (fd >= 0) {
			i = fcntl(fd, F_GETFD);
			if (i >= 0) 
				fcntl(fd, F_SETFD, i | FD_CLOEXEC);
		}
		srand((getpid() << 16) ^ getuid() ^ tv.tv_sec ^ tv.tv_usec);
#ifdef DO_JRAND_MIX
		jrand_seed[0] = getpid() ^ (tv.tv_sec & 0xFFFF);
		jrand_seed[1] = getppid() ^ (tv.tv_usec & 0xFFFF);
		jrand_seed[2] = (tv.tv_sec ^ tv.tv_usec) >> 16;
#endif
	}
	/* Crank the random number generator a few times */
	gettimeofday(&tv, 0);
	for (i = (tv.tv_sec ^ tv.tv_usec) & 0x1F; i > 0; i--)
		rand();
	return fd;
}


/*
 * Generate a series of random bytes.  Use /dev/urandom if possible,
 * and if not, use srandom/random.
 */
static void get_random_bytes(void *buf, int nbytes)
{
	int i, n = nbytes, fd = get_random_fd();
	int lose_counter = 0;
	unsigned char *cp = (unsigned char *) buf;
	unsigned short tmp_seed[3];

	if (fd >= 0) {
		while (n > 0) {
			i = read(fd, cp, n);
			if (i <= 0) {
				if (lose_counter++ > 16)
					break;
				continue;
			}
			n -= i;
			cp += i;
			lose_counter = 0;
		}
	}
	
	/*
	 * We do this all the time, but this is the only source of
	 * randomness if /dev/random/urandom is out to lunch.
	 */
	for (cp = buf, i = 0; i < nbytes; i++)
		*cp++ ^= (rand() >> 7) & 0xFF;
#ifdef DO_JRAND_MIX
	memcpy(tmp_seed, jrand_seed, sizeof(tmp_seed));
	jrand_seed[2] = jrand_seed[2] ^ syscall(__NR_gettid);
	for (cp = buf, i = 0; i < nbytes; i++)
		*cp++ ^= (jrand48(tmp_seed) >> 7) & 0xFF;
	memcpy(jrand_seed, tmp_seed, 
	       sizeof(jrand_seed)-sizeof(unsigned short));
#endif

	return;
}

/*
 * Get the ethernet hardware address, if we can find it...
 */
static int get_node_id(unsigned char *node_id)
{
#ifdef HAVE_NET_IF_H
	int 		sd;
	struct ifreq 	ifr, *ifrp;
	struct ifconf 	ifc;
	char buf[1024];
	int		n, i;
	unsigned char 	*a;
#ifdef HAVE_NET_IF_DL_H
	struct sockaddr_dl *sdlp;
#endif

/*
 * BSD 4.4 defines the size of an ifreq to be
 * max(sizeof(ifreq), sizeof(ifreq.ifr_name)+ifreq.ifr_addr.sa_len
 * However, under earlier systems, sa_len isn't present, so the size is 
 * just sizeof(struct ifreq)
 */
#ifdef HAVE_SA_LEN
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN*/

	sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0) {
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl (sd, SIOCGIFCONF, (char *)&ifc) < 0) {
		close(sd);
		return -1;
	}
	n = ifc.ifc_len;
	for (i = 0; i < n; i+= ifreq_size(*ifrp) ) {
		ifrp = (struct ifreq *)((char *) ifc.ifc_buf+i);
		strncpy(ifr.ifr_name, ifrp->ifr_name, IFNAMSIZ);
#ifdef SIOCGIFHWADDR
		if (ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) &ifr.ifr_hwaddr.sa_data;
#else
#ifdef SIOCGENADDR
		if (ioctl(sd, SIOCGENADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) ifr.ifr_enaddr;
#else
#ifdef HAVE_NET_IF_DL_H
		sdlp = (struct sockaddr_dl *) &ifrp->ifr_addr;
		if ((sdlp->sdl_family != AF_LINK) || (sdlp->sdl_alen != 6))
			continue;
		a = (unsigned char *) &sdlp->sdl_data[sdlp->sdl_nlen];
#else
		/*
		 * XXX we don't have a way of getting the hardware
		 * address
		 */
		close(sd);
		return 0;
#endif /* HAVE_NET_IF_DL_H */
#endif /* SIOCGENADDR */
#endif /* SIOCGIFHWADDR */
		if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
			continue;
		if (node_id) {
			memcpy(node_id, a, 6);
			close(sd);
			return 1;
		}
	}
	close(sd);
#endif
	return 0;
}

/* Assume that the gettimeofday() has microsecond granularity */
#define MAX_ADJUSTMENT 10

static int get_clock(uint32_t *clock_high, uint32_t *clock_low, uint16_t *ret_clock_seq)
{
	static int			adjustment = 0;
	static struct timeval		last = {0, 0};
	static uint16_t			clock_seq;
	struct timeval 			tv;
	unsigned long long		clock_reg;
	
try_again:
	gettimeofday(&tv, 0);
	if ((last.tv_sec == 0) && (last.tv_usec == 0)) {
		get_random_bytes(&clock_seq, sizeof(clock_seq));
		clock_seq &= 0x3FFF;
		last = tv;
		last.tv_sec--;
	}
	if ((tv.tv_sec < last.tv_sec) ||
	    ((tv.tv_sec == last.tv_sec) &&
	     (tv.tv_usec < last.tv_usec))) {
		clock_seq = (clock_seq+1) & 0x3FFF;
		adjustment = 0;
		last = tv;
	} else if ((tv.tv_sec == last.tv_sec) &&
	    (tv.tv_usec == last.tv_usec)) {
		if (adjustment >= MAX_ADJUSTMENT)
			goto try_again;
		adjustment++;
	} else {
		adjustment = 0;
		last = tv;
	}
		
	clock_reg = tv.tv_usec*10 + adjustment;
	clock_reg += ((unsigned long long) tv.tv_sec)*10000000;
	clock_reg += (((unsigned long long) 0x01B21DD2) << 32) + 0x13814000;

	*clock_high = clock_reg >> 32;
	*clock_low = clock_reg;
	*ret_clock_seq = clock_seq;
	return 0;
}

#if HAS_RANDOMTIME_FUNCTION
void uuid_generate_time(uuid_t out)
{
	static unsigned char node_id[6];
	static int has_init = 0;
	struct uuid uu;
	uint32_t	clock_mid;

	//printf("uuid_generate_time\n");
	if (!has_init) {
		if (get_node_id(node_id) <= 0) {
			get_random_bytes(node_id, 6);
			//printf("get_random_bytes %x, %x, %x, %x, %x, %x \n", 
			//	node_id[0], node_id[1], node_id[2], node_id[3], node_id[4], node_id[5] );
			/*
			 * Set multicast bit, to prevent conflicts
			 * with IEEE 802 addresses obtained from
			 * network cards
			 */
			node_id[0] |= 0x01;
		}
		has_init = 1;
	}
	get_clock(&clock_mid, &uu.time_low, &uu.clock_seq);
	uu.clock_seq |= 0x8000;
	uu.time_mid = (uint16_t) clock_mid;
	uu.time_hi_and_version = ((clock_mid >> 16) & 0x0FFF) | 0x1000;
	memcpy(uu.node, node_id, 6);
	uuid_pack(&uu, out);
}
#endif

#if HAS_URANDOM_FUNTION
void uuid_generate_random(uuid_t out)
{
	uuid_t	buf;
	struct uuid uu;

	//printf("uuid_generate_random\n");
	get_random_bytes(buf, sizeof(buf));
	uuid_unpack(buf, &uu);

	uu.clock_seq = (uu.clock_seq & 0x3FFF) | 0x8000;
	uu.time_hi_and_version = (uu.time_hi_and_version & 0x0FFF) | 0x4000;
	uuid_pack(&uu, out);
}
#endif

#define UUID_FILE "/etc/uuid.txt"
#define MAGIC_NUM "18de43f8-677c-4d3a-8920-"

void uuid_generate_mac(uuid_t out, const char *mac_str)
{
	FILE *fd;

	fd = fopen(UUID_FILE, "w");
	
	fprintf(fd, "%s%s", MAGIC_NUM, mac_str);
	fclose(fd);
}


/*
 * This is the generic front-end to uuid_generate_random and
 * uuid_generate_time.  It uses uuid_generate_random only if
 * /dev/urandom is available, since otherwise we won't have
 * high-quality randomness.
 */
#if HAS_URANDOM_FUNTION || HAS_RANDOMTIME_FUNCTION
void uuid_generate(uuid_t out)
{
	if (get_random_fd() >= 0)
		uuid_generate_random(out);
	else
		uuid_generate_time(out);
}
#endif


int main(int argc, char *argv[])
{
    uuid_t out;
    unsigned char *p;
    int i;
    FILE *fd;

    if ((argc == 1) ||
		(argc != 3 && (strcmp("-m", argv[1]) == 0))
#if HAS_RANDOMTIME_FUNCTION
		 || (argc != 2 && (strcmp("-t", argv[1]) == 0))
#endif
#if HAS_URANDOM_FUNTION
		 || (argc != 2 && (strcmp("-r", argv[1]) == 0))
#endif
        )
	{
#if HAS_URANDOM_FUNTION
		printf("uuidgen -r\n");
		printf("          Generate a random-based UUID\n");
#endif
#if HAS_RANDOMTIME_FUNCTION
		printf("uuidgen -t\n");
		printf("          Generate a time-based UUID\n");
#endif
		printf("uuidgen -m mac_addr\n");
		printf("          Generate a mac-based UUID\n");
		return 0;
	}

#if HAS_RANDOMTIME_FUNCTION
    if(strcmp("-t", argv[1]) == 0)
    {
        uuid_generate_time(out);
    }else 
#endif
   if(strcmp("-m", argv[1]) == 0)
    {
		if (argv[2] && strlen(argv[2]) == 12)
        {
			uuid_generate_mac(out, argv[2]);
			return 0;
		}
		else
		{
#if HAS_URANDOM_FUNTION
			uuid_generate_random(out);
#else
                        printf("uuidgen -m ERROR! please check!\n");
                        return -1;
#endif
		}
    }
#if HAS_URANDOM_FUNTION
    else 
    {
	if (get_random_fd() >= 0)
		(out);
    }
#endif    
    p=(unsigned char *)out;
    
    for(i=0; i<16; i++)
    {
        printf("%.2x", p[i]);
        if((3==i)||(5==i)||(7==i)||(9==i))
        {
            printf("-");
        }
    }
    printf("\n");
    

    fd = fopen(UUID_FILE, "w");

    if(NULL==fd)
      return -1;

    for(i=0; i<16; i++)
    {
        fprintf(fd, "%.2x", p[i]);
        if((3==i)||(5==i)||(7==i)||(9==i))
        {
            fprintf(fd, "-");
        }
    }
    
    fclose(fd);
    
    return 0;
}