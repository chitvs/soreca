/*
 * This is part of the tenth session.
 *
 * Goals:
 * - Implement a Simple DNS Server over UDP.
 * - Implement a Simple VPN Server over UDP.
 * - Understand the basic structure of DNS protocol (RFC 1035).
 * - Handle binary data decoding/encoding over network sockets.
 * - Understand how Virtual Private Networks establish tunnels.
 * - Learn how to interact with Linux TUN/TAP network interfaces.
 * - Handle routing tables and masquerading via iptables.
 * - Read/Write raw IP packets to a TUN device and forward them via UDP.
 *
 * Exercise 2 - Simple VPN Server
 *
 * This code can be compiled both as a VPN Server and a VPN Client using 
 * preprocessor directives (-DAS_CLIENT).
 * The application creates a virtual network interface (tun0 or tun1),
 * configures the routing table to hijack traffic, and listens on a UDP port.
 * It uses select() to multiplex I/O. When an IP packet arrives on the TUN 
 * interface, it is read, "encrypted", and sent via UDP to the peer. When a 
 * UDP datagram arrives from the peer, it is "decrypted", and written back 
 * into the local TUN interface to be routed by the OS kernel.
 */

#define _GNU_SOURCE /* just to disable errors on intellisense */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/if.h>
#include <linux/if_tun.h>

/*
 * Configuration directives
 * These are passed to the precompiler via Makefile.
 * E.g., for the client: CLNTFLAGS = -DAS_CLIENT=YES -DSERVER_HOST="\"10.8.0.1\""
 * WARNING: ensure proper escaping of double quotes (\") in the Makefile.
 */

/* #define AS_CLIENT YES */
/* #define SERVER_HOST "10.8.0.1" */

#define PORT 54345
#define MTU 1400
#define BIND_HOST "0.0.0.0"

static int max(int a, int b) {
    return a > b ? a : b;
}

/*
 * TUN allocator
 *
 * Creates the VPN interface (/dev/tun0 or /dev/tun1), configures it,
 * and returns its file descriptor. Uses ioctl with TUNSETIFF.
 */
int tun_alloc() {
    struct ifreq ifr;
    int fd, e;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("Cannot open /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
#ifdef AS_CLIENT
    strncpy(ifr.ifr_name, "tun1", IFNAMSIZ);
#else
    strncpy(ifr.ifr_name, "tun0", IFNAMSIZ);
#endif

    if ((e = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
        perror("ioctl[TUNSETIFF]");
        close(fd);
        return e;
    }

    return fd;
}

/*
 * Execute commands wrapper
 */
static void run(char *cmd) {
    printf("Execute `%s`\n", cmd);
    if (system(cmd)) {
        perror(cmd);
        exit(1);
    }
}

/*
 * Network configuration
 *
 * Configures the IP address and MTU of the VPN TUN interface.
 */
void ifconfig() {
    char cmd[1024];

#ifdef AS_CLIENT
    snprintf(cmd, sizeof(cmd), "ifconfig tun1 10.8.0.2/16 mtu %d up", MTU);
#else
    snprintf(cmd, sizeof(cmd), "ifconfig tun0 10.8.0.1/16 mtu %d up", MTU);
#endif
    run(cmd);
}

/*
 * Setup route table
 *
 * Configures the OS routing table using `iptables` and `ip route`
 * to forward the desired traffic into the TUN interface.
 */
void setup_route_table() {

    run("sysctl -w net.ipv4.ip_forward=1");

#ifdef AS_CLIENT
    run("iptables -t nat -A POSTROUTING -o tun1 -j MASQUERADE");
    run("iptables -I FORWARD 1 -i tun1 -m state --state RELATED,ESTABLISHED -j ACCEPT");
    run("iptables -I FORWARD 1 -o tun1 -j ACCEPT");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ip route add %s via $(ip route show 0/0 | sed -e 's/.* via \\([^ ]*\\).*/\\1/')", SERVER_HOST);
    run(cmd);
    run("ip route add 0/1 dev tun1");
    run("ip route add 128/1 dev tun1");
#else
    run("iptables -t nat -A POSTROUTING -s 10.8.0.0/16 ! -d 10.8.0.0/16 -m comment --comment 'vpndemo' -j MASQUERADE");
    run("iptables -A FORWARD -s 10.8.0.0/16 -m state --state RELATED,ESTABLISHED -j ACCEPT");
    run("iptables -A FORWARD -d 10.8.0.0/16 -j ACCEPT");
#endif
}

/*
 * Cleanup route table
 *
 * Restores the OS routing table to its previous state upon exit.
 */
void cleanup_route_table() {
#ifdef AS_CLIENT
    run("iptables -t nat -D POSTROUTING -o tun1 -j MASQUERADE");
    run("iptables -D FORWARD -i tun1 -m state --state RELATED,ESTABLISHED -j ACCEPT");
    run("iptables -D FORWARD -o tun1 -j ACCEPT");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ip route del %s", SERVER_HOST);
    run(cmd);
    run("ip route del 0/1");
    run("ip route del 128/1");
#else
    run("iptables -t nat -D POSTROUTING -s 10.8.0.0/16 ! -d 10.8.0.0/16 -m comment --comment 'vpndemo' -j MASQUERADE");
    run("iptables -D FORWARD -s 10.8.0.0/16 -m state --state RELATED,ESTABLISHED -j ACCEPT");
    run("iptables -D FORWARD -d 10.8.0.0/16 -j ACCEPT");
#endif
}

/*
 * UDP bind
 *
 * Opens a UDP socket for establishing the VPN tunnel.
 * If running as a server, it binds the socket to the port.
 */
int udp_bind(struct sockaddr *addr, socklen_t *addrlen) {
    struct addrinfo hints;
    struct addrinfo *result;
    int sock, flags;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

#ifdef AS_CLIENT
    const char *host = SERVER_HOST;
#else
    const char *host = BIND_HOST;
#endif
    if (0 != getaddrinfo(host, NULL, &hints, &result)) {
        perror("getaddrinfo error");
        return -1;
    }

    if (result->ai_family == AF_INET)
        ((struct sockaddr_in *)result->ai_addr)->sin_port = htons(PORT);
    else if (result->ai_family == AF_INET6)
        ((struct sockaddr_in6 *)result->ai_addr)->sin6_port = htons(PORT);
    else {
        fprintf(stderr, "unknown ai_family %d", result->ai_family);
        freeaddrinfo(result);
        return -1;
    }
    memcpy(addr, result->ai_addr, result->ai_addrlen);
    *addrlen = result->ai_addrlen;

    if (-1 == (sock = socket(result->ai_family, SOCK_DGRAM, IPPROTO_UDP))) {
        perror("Cannot create socket");
        freeaddrinfo(result);
        return -1;
    }

#ifndef AS_CLIENT
    if (0 != bind(sock, result->ai_addr, result->ai_addrlen)) {
        perror("Cannot bind");
        close(sock);
        freeaddrinfo(result);
        return -1;
    }
#endif

    freeaddrinfo(result);

    /* Set socket to non-blocking mode */
    flags = fcntl(sock, F_GETFL, 0);
    if (flags != -1) {
        if (-1 != fcntl(sock, F_SETFL, flags | O_NONBLOCK))
            return sock;
    }
    perror("fcntl error");

    close(sock);
    return -1;
}

/*
 * Catch Ctrl-C and kill signals.
 *
 * Ensures the route table gets cleaned before this process exits.
 */
void cleanup(int signo) {
    printf("Goodbye, cruel world....\n");
    if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM) {
        cleanup_route_table();
        exit(0);
    }
}

void cleanup_when_sig_exit() {
    struct sigaction sa;
    sa.sa_handler = &cleanup;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);

    if (sigaction(SIGHUP, &sa, NULL) < 0) perror("Cannot handle SIGHUP");
    if (sigaction(SIGINT, &sa, NULL) < 0) perror("Cannot handle SIGINT");
    if (sigaction(SIGTERM, &sa, NULL) < 0) perror("Cannot handle SIGTERM");
}

/*
 * Encryption stubs
 *
 * For a real-world VPN, traffic inside the UDP tunnel is encrypted.
 * A comprehensive encryption is not easy and not the point for this educational demo.
 */
void encrypt(char *plantext, char *ciphertext, int len) {
    memcpy(ciphertext, plantext, len);
}

void decrypt(char *ciphertext, char *plantext, int len) {
    memcpy(plantext, ciphertext, len);
}

int main(int argc, char **argv) {
    int tun_fd;
    if ((tun_fd = tun_alloc()) < 0) return 1;

    ifconfig();
    setup_route_table();
    cleanup_when_sig_exit();

    int udp_fd;
    struct sockaddr_storage client_addr;
    socklen_t client_addrlen = sizeof(client_addr);

    if ((udp_fd = udp_bind((struct sockaddr *)&client_addr, &client_addrlen)) < 0) return 1;

    /*
     * tun_buf: memory buffer read from/write to tun dev (always plain IP packets).
     * udp_buf: memory buffer read from/write to udp fd (always encrypted datagrams).
     */
    char tun_buf[MTU], udp_buf[MTU];
    bzero(tun_buf, MTU);
    bzero(udp_buf, MTU);

    while (1) {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(tun_fd, &readset);
        FD_SET(udp_fd, &readset);
        int max_fd = max(tun_fd, udp_fd) + 1;

        if (-1 == select(max_fd, &readset, NULL, NULL, NULL)) {
            perror("select error");
            break;
        }

        int r;
        
        /*
         * Read plain IP packets from the TUN device, encrypt them, 
         * and send them into the UDP tunnel.
         */
        if (FD_ISSET(tun_fd, &readset)) {
            r = read(tun_fd, tun_buf, MTU);
            if (r < 0) {
                perror("read from tun_fd error");
                break;
            }

            encrypt(tun_buf, udp_buf, r);
            printf("Writing to UDP %d bytes ...\n", r);
            
            r = sendto(udp_fd, udp_buf, r, 0, (const struct sockaddr *)&client_addr, client_addrlen);
            if (r < 0) {
                perror("sendto udp_fd error");
                break;
            }
        }

        /*
         * Read encrypted datagrams from the UDP tunnel, decrypt them, 
         * and write the raw IP packets back into the TUN device.
         */
        if (FD_ISSET(udp_fd, &readset)) {
            r = recvfrom(udp_fd, udp_buf, MTU, 0, (struct sockaddr *)&client_addr, &client_addrlen);
            if (r < 0) {
                perror("recvfrom udp_fd error");
                break;
            }

            decrypt(udp_buf, tun_buf, r);
            printf("Writing to tun %d bytes ...\n", r);
            
            r = write(tun_fd, tun_buf, r);
            if (r < 0) {
                perror("write tun_fd error");
                break;
            }
        }
    }

    close(tun_fd);
    close(udp_fd);

    cleanup_route_table();

    return 0;
}
