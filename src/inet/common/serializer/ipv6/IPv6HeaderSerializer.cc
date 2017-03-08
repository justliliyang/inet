//
// Copyright (C) 2013 Irene Ruengeler
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#include "inet/common/packet/SerializerRegistry.h"
#include "inet/common/serializer/ipv6/headers/ip6.h"
#include "inet/common/serializer/ipv6/IPv6HeaderSerializer.h"
#include "inet/networklayer/ipv6/IPv6ExtensionHeaders.h"
#include "inet/networklayer/ipv6/IPv6Header.h"

#if defined(_MSC_VER)
#undef s_addr    /* MSVC #definition interferes with us */
#endif // if defined(_MSC_VER)

#if !defined(_WIN32) && !defined(__WIN32__) && !defined(WIN32) && !defined(__CYGWIN__) && !defined(_WIN64)
#include <netinet/in.h>    // htonl, ntohl, ...
#endif // if !defined(_WIN32) && !defined(__WIN32__) && !defined(WIN32) && !defined(__CYGWIN__) && !defined(_WIN64)

// This in_addr field is defined as a macro in Windows and Solaris, which interferes with us
#undef s_addr

namespace inet {

namespace serializer {

Register_Serializer(IPv6Header, IPv6HeaderSerializer);

void IPv6HeaderSerializer::serialize(ByteOutputStream& stream, const std::shared_ptr<Chunk>& chunk) const
{
    const auto& dgram = std::static_pointer_cast<const IPv6Header>(chunk);
    unsigned int i;
    uint32_t flowinfo;

    EV << "Serialize IPv6 packet\n";

    unsigned int nextHdrCodePos = stream.getPosition() + 6;
    struct ip6_hdr ip6h;

    flowinfo = 0x06;
    flowinfo <<= 8;
    flowinfo |= dgram->getTrafficClass();
    flowinfo <<= 20;
    flowinfo |= dgram->getFlowLabel();
    ip6h.ip6_flow = htonl(flowinfo);
    ip6h.ip6_hlim = htons(dgram->getHopLimit());

    ip6h.ip6_nxt = dgram->getExtensionHeaderArraySize() != 0 ? dgram->getExtensionHeader(0)->getExtensionType() : dgram->getTransportProtocol();

    for (i = 0; i < 4; i++) {
        ip6h.ip6_src.__u6_addr.__u6_addr32[i] = htonl(dgram->getSrcAddress().words()[i]);
    }
    for (i = 0; i < 4; i++) {
        ip6h.ip6_dst.__u6_addr.__u6_addr32[i] = htonl(dgram->getDestAddress().words()[i]);
    }

    ip6h.ip6_plen = htons(dgram->getPayloadLength());

    for (int i = 0; i < IPv6_HEADER_BYTES; i++)
        stream.writeByte(((uint8_t *)&ip6h)[i]);

    //FIXME serialize extension headers
    for (i = 0; i < dgram->getExtensionHeaderArraySize(); i++) {
        const IPv6ExtensionHeader *extHdr = dgram->getExtensionHeader(i);
        stream.writeByte(i + 1 < dgram->getExtensionHeaderArraySize() ? dgram->getExtensionHeader(i + 1)->getExtensionType() : dgram->getTransportProtocol());
        ASSERT((extHdr->getByteLength() & 7) == 0);
        stream.writeByte((extHdr->getByteLength() - 8) / 8);
        switch (extHdr->getExtensionType()) {
            case IP_PROT_IPv6EXT_HOP: {
                const IPv6HopByHopOptionsHeader *hdr = check_and_cast<const IPv6HopByHopOptionsHeader *>(extHdr);
                stream.writeByteRepeatedly(0, hdr->getByteLength() - 2);    //TODO
                break;
            }
            case IP_PROT_IPv6EXT_DEST: {
                const IPv6DestinationOptionsHeader *hdr = check_and_cast<const IPv6DestinationOptionsHeader *>(extHdr);
                stream.writeByteRepeatedly(0, hdr->getByteLength() - 2);    //TODO
                break;
            }
            case IP_PROT_IPv6EXT_ROUTING: {
                const IPv6RoutingHeader *hdr = check_and_cast<const IPv6RoutingHeader *>(extHdr);
                stream.writeByte(hdr->getRoutingType());
                stream.writeByte(hdr->getSegmentsLeft());
                for (unsigned int j = 0; j < hdr->getAddressArraySize(); j++) {
                    stream.writeIPv6Address(hdr->getAddress(j));
                }
                stream.writeByteRepeatedly(0, 4);
                break;
            }
            case IP_PROT_IPv6EXT_FRAGMENT: {
                const IPv6FragmentHeader *hdr = check_and_cast<const IPv6FragmentHeader *>(extHdr);
                ASSERT((hdr->getFragmentOffset() & 7) == 0);
                stream.writeUint16(hdr->getFragmentOffset() | (hdr->getMoreFragments() ? 1 : 0));
                stream.writeUint32(hdr->getIdentification());
                break;
            }
            case IP_PROT_IPv6EXT_AUTH: {
                const IPv6AuthenticationHeader *hdr = check_and_cast<const IPv6AuthenticationHeader *>(extHdr);
                stream.writeByteRepeatedly(0, hdr->getByteLength() - 2);    //TODO
                break;
            }
            case IP_PROT_IPv6EXT_ESP: {
                const IPv6EncapsulatingSecurityPayloadHeader *hdr = check_and_cast<const IPv6EncapsulatingSecurityPayloadHeader *>(extHdr);
                stream.writeByteRepeatedly(0, hdr->getByteLength() - 2);    //TODO
                break;
            }
            default: {
                throw cRuntimeError("Unknown IPv6 extension header %d (%s)%s", extHdr->getExtensionType(), extHdr->getClassName(), extHdr->getFullName());
                break;
            }
        }
        ASSERT(nextHdrCodePos + extHdr->getByteLength() == stream.getPosition());
    }
}

std::shared_ptr<Chunk> IPv6HeaderSerializer::deserialize(ByteInputStream& stream) const
{
    uint8_t buffer[IPv6_HEADER_BYTES];
    for (int i = 0; i < IPv6_HEADER_BYTES; i++)
        buffer[i] = stream.readByte();
    auto dest = std::make_shared<IPv6Header>();
    const struct ip6_hdr& ip6h = *static_cast<const struct ip6_hdr *>((void *)&buffer);
    uint32_t flowinfo = ntohl(ip6h.ip6_flow);
    dest->setFlowLabel(flowinfo & 0xFFFFF);
    flowinfo >>= 20;
    dest->setTrafficClass(flowinfo & 0xFF);

    dest->setTransportProtocol(ip6h.ip6_nxt);
    dest->setHopLimit(ntohs(ip6h.ip6_hlim));

    IPv6Address temp;
    temp.set(ntohl(ip6h.ip6_src.__u6_addr.__u6_addr32[0]),
             ntohl(ip6h.ip6_src.__u6_addr.__u6_addr32[1]),
             ntohl(ip6h.ip6_src.__u6_addr.__u6_addr32[2]),
             ntohl(ip6h.ip6_src.__u6_addr.__u6_addr32[3]));
    dest->setSrcAddress(temp);

    temp.set(ntohl(ip6h.ip6_dst.__u6_addr.__u6_addr32[0]),
             ntohl(ip6h.ip6_dst.__u6_addr.__u6_addr32[1]),
             ntohl(ip6h.ip6_dst.__u6_addr.__u6_addr32[2]),
             ntohl(ip6h.ip6_dst.__u6_addr.__u6_addr32[3]));
    dest->setDestAddress(temp);
    dest->setPayloadLength(ip6h.ip6_plen);
    dest->setChunkLength(byte(IPv6_HEADER_BYTES));

    return dest;
}

} // namespace serializer

} // namespace inet

