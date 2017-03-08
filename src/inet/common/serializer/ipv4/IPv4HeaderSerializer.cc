//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "inet/common/packet/SerializerRegistry.h"
#include "inet/common/serializer/headers/bsdint.h"
#include "inet/common/serializer/headers/defs.h"
#include "inet/common/serializer/headers/in_systm.h"
#include "inet/common/serializer/headers/in.h"
#include "inet/common/serializer/ipv4/headers/ip.h"
#include "inet/common/serializer/ipv4/IPv4HeaderSerializer.h"

namespace inet {

namespace serializer {

Register_Serializer(IPv4Header, IPv4HeaderSerializer);

void IPv4HeaderSerializer::serialize(ByteOutputStream& stream, const std::shared_ptr<Chunk>& chunk) const
{
    auto position = stream.getPosition();
    struct ip iphdr;
    const auto& ipv4Header = std::static_pointer_cast<const IPv4Header>(chunk);
    unsigned int headerLength = ipv4Header->getHeaderLength();
    ASSERT((headerLength & 3) == 0);
    iphdr.ip_hl = headerLength >> 2;
    iphdr.ip_v = ipv4Header->getVersion();
    iphdr.ip_tos = ipv4Header->getTypeOfService();
    iphdr.ip_id = htons(ipv4Header->getIdentification());
    ASSERT((ipv4Header->getFragmentOffset() & 7) == 0);
    uint16_t ip_off = ipv4Header->getFragmentOffset() / 8;
    if (ipv4Header->getMoreFragments())
        ip_off |= IP_MF;
    if (ipv4Header->getDontFragment())
        ip_off |= IP_DF;
    iphdr.ip_off = htons(ip_off);
    iphdr.ip_ttl = ipv4Header->getTimeToLive();
    iphdr.ip_p = ipv4Header->getTransportProtocol();
    iphdr.ip_src.s_addr = htonl(ipv4Header->getSrcAddress().getInt());
    iphdr.ip_dst.s_addr = htonl(ipv4Header->getDestAddress().getInt());
    iphdr.ip_len = htons(ipv4Header->getTotalLengthField());
    if (ipv4Header->getCrcMode() != CRC_COMPUTED)
        throw cRuntimeError("Cannot serialize IPv4 header without a properly computed CRC");
    iphdr.ip_sum = htons(ipv4Header->getCrc());

    if (headerLength > IP_HEADER_BYTES) {
        throw cRuntimeError("ideiglenesen");
        unsigned short numOptions = ipv4Header->getOptionArraySize();
        unsigned int optionsLength = 0;
        if (numOptions > 0) {    // options present?
            for (unsigned short i = 0; i < numOptions; i++) {
                const TLVOptionBase *option = &ipv4Header->getOption(i);
                serializeOption(stream, option);
                optionsLength += option->getLength();
            }
        }    // if options present
        if (ipv4Header->getHeaderLength() < (int)(IP_HEADER_BYTES + optionsLength))
            throw cRuntimeError("Serializing an IPv4 packet with wrong headerLength value: not enough for store options.\n");
        auto writtenLength = stream.getPosition() - position;
        if (writtenLength < headerLength)
            stream.writeByteRepeatedly(IPOPTION_END_OF_OPTIONS, headerLength - writtenLength);
    }

    for (int i = 0; i < IP_HEADER_BYTES; i++)
        stream.writeByte(((uint8_t *)&iphdr)[i]);
}

void IPv4HeaderSerializer::serializeOption(ByteOutputStream& stream, const TLVOptionBase *option) const
{
    unsigned short type = option->getType();
    unsigned short length = option->getLength();    // length >= 1

    stream.writeByte(type);
    if (length > 1)
        stream.writeByte(length);

    auto *opt = dynamic_cast<const TLVOptionRaw *>(option);
    if (opt) {
        unsigned int datalen = opt->getBytesArraySize();
        ASSERT(length == 2 + datalen);
        for (unsigned int i = 0; i < datalen; i++)
            stream.writeByte(opt->getBytes(i));
        return;
    }

    switch (type) {
        case IPOPTION_END_OF_OPTIONS:
            check_and_cast<const IPv4OptionEnd *>(option);
            ASSERT(length == 1);
            break;

        case IPOPTION_NO_OPTION:
            check_and_cast<const IPv4OptionNop *>(option);
            ASSERT(length == 1);
            break;

        case IPOPTION_STREAM_ID: {
            auto *opt = check_and_cast<const IPv4OptionStreamId *>(option);
            ASSERT(length == 4);
            stream.writeUint16(opt->getStreamId());
            break;
        }

        case IPOPTION_TIMESTAMP: {
            auto *opt = check_and_cast<const IPv4OptionTimestamp *>(option);
            int bytes = (opt->getFlag() == IP_TIMESTAMP_TIMESTAMP_ONLY) ? 4 : 8;
            ASSERT(length == 4 + bytes * opt->getRecordTimestampArraySize());
            uint8_t pointer = 5 + opt->getNextIdx() * bytes;
            stream.writeByte(pointer);
            uint8_t flagbyte = opt->getOverflow() << 4 | opt->getFlag();
            stream.writeByte(flagbyte);
            for (unsigned int count = 0; count < opt->getRecordTimestampArraySize(); count++) {
                if (bytes == 8)
                    stream.writeIPv4Address(opt->getRecordAddress(count));
                stream.writeUint32(opt->getRecordTimestamp(count).inUnit(SIMTIME_MS));
            }
            break;
        }

        case IPOPTION_RECORD_ROUTE:
        case IPOPTION_LOOSE_SOURCE_ROUTING:
        case IPOPTION_STRICT_SOURCE_ROUTING: {
            auto *opt = check_and_cast<const IPv4OptionRecordRoute *>(option);
            ASSERT(length == 3 + 4 * opt->getRecordAddressArraySize());
            uint8_t pointer = 4 + opt->getNextAddressIdx() * 4;
            stream.writeByte(pointer);
            for (unsigned int count = 0; count < opt->getRecordAddressArraySize(); count++) {
                stream.writeIPv4Address(opt->getRecordAddress(count));
            }
            break;
        }
        case IPOPTION_ROUTER_ALERT:
        case IPOPTION_SECURITY:
        default: {
            throw cRuntimeError("Unknown IPv4Option type=%d (not in an TLVOptionRaw option)", type);
            break;
        }
    }
}

std::shared_ptr<Chunk> IPv4HeaderSerializer::deserialize(ByteInputStream& stream) const
{
    auto position = stream.getPosition();
    uint8_t buffer[IP_HEADER_BYTES];
    for (int i = 0; i < IP_HEADER_BYTES; i++)
        buffer[i] = stream.readByte();
    auto ipv4Header = std::make_shared<IPv4Header>();
    unsigned int bufsize = stream.getRemainingSize();
    const struct ip& iphdr = *static_cast<const struct ip *>((void *)&buffer);
    unsigned int totalLength, headerLength;

    ipv4Header->setVersion(iphdr.ip_v);
    ipv4Header->setHeaderLength(IP_HEADER_BYTES);
    ipv4Header->setSrcAddress(IPv4Address(ntohl(iphdr.ip_src.s_addr)));
    ipv4Header->setDestAddress(IPv4Address(ntohl(iphdr.ip_dst.s_addr)));
    ipv4Header->setTransportProtocol(iphdr.ip_p);
    ipv4Header->setTimeToLive(iphdr.ip_ttl);
    ipv4Header->setIdentification(ntohs(iphdr.ip_id));
    uint16_t ip_off = ntohs(iphdr.ip_off);
    ipv4Header->setMoreFragments((ip_off & IP_MF) != 0);
    ipv4Header->setDontFragment((ip_off & IP_DF) != 0);
    ipv4Header->setFragmentOffset((ntohs(iphdr.ip_off) & IP_OFFMASK) * 8);
    ipv4Header->setTypeOfService(iphdr.ip_tos);
    totalLength = ntohs(iphdr.ip_len);
    ipv4Header->setTotalLengthField(totalLength);
    headerLength = iphdr.ip_hl << 2;

    if (headerLength < IP_HEADER_BYTES) {
        ipv4Header->markIncorrect();
        headerLength = IP_HEADER_BYTES;
    }

    ipv4Header->setHeaderLength(headerLength);

    if (headerLength > IP_HEADER_BYTES) {    // options present?
        while (stream.getPosition() - position < headerLength) {
            TLVOptionBase *option = deserializeOption(stream);
            ipv4Header->addOption(option);
        }
    }
    ipv4Header->setCrc(iphdr.ip_sum);
    ipv4Header->setCrcMode(CRC_COMPUTED);

    if (totalLength > bufsize)
        EV_ERROR << "Can not handle IPv4 packet of total length " << totalLength << "(captured only " << bufsize << " bytes).\n";

    ipv4Header->setHeaderLength(headerLength);
    return ipv4Header;
}

TLVOptionBase *IPv4HeaderSerializer::deserializeOption(ByteInputStream& stream) const
{
    auto position = stream.getPosition();
    unsigned char type = stream.readByte();
    unsigned char length = 1;

    switch (type) {
        case IPOPTION_END_OF_OPTIONS:    // EOL
            return new IPv4OptionEnd();

        case IPOPTION_NO_OPTION:    // NOP
            return new IPv4OptionNop();

        case IPOPTION_STREAM_ID:
            length = stream.readByte();
            if (length == 4) {
                auto *option = new IPv4OptionStreamId();
                option->setType(type);
                option->setLength(length);
                option->setStreamId(stream.readUint16());
                return option;
            }
            break;

        case IPOPTION_TIMESTAMP: {
            length = stream.readByte();
            uint8_t pointer = stream.readByte();
            uint8_t flagbyte = stream.readByte();
            uint8_t overflow = flagbyte >> 4;
            int flag = -1;
            int bytes = 0;
            switch (flagbyte & 0x0f) {
                case 0: flag = IP_TIMESTAMP_TIMESTAMP_ONLY; bytes = 4; break;
                case 1: flag = IP_TIMESTAMP_WITH_ADDRESS; bytes = 8; break;
                case 3: flag = IP_TIMESTAMP_SENDER_INIT_ADDRESS; bytes = 8; break;
                default: break;
            }
            if (flag != -1 && length > 4 && bytes && ((length-4) % bytes) == 0 && pointer >= 5 && ((pointer-5) % bytes) == 0) {
                auto *option = new IPv4OptionTimestamp();
                option->setType(type);
                option->setLength(length);
                option->setFlag(flag);
                option->setOverflow(overflow);
                option->setRecordTimestampArraySize((length - 4) / bytes);
                if (bytes == 8)
                    option->setRecordAddressArraySize((length - 4) / bytes);
                option->setNextIdx((pointer-5) / bytes);
                for (unsigned int count = 0; count < option->getRecordAddressArraySize(); count++) {
                    if (bytes == 8)
                        option->setRecordAddress(count, stream.readIPv4Address());
                    option->setRecordTimestamp(count, SimTime(stream.readUint32(), SIMTIME_MS));
                }
                return option;
            }
            break;
        }

        case IPOPTION_RECORD_ROUTE:
        case IPOPTION_LOOSE_SOURCE_ROUTING:
        case IPOPTION_STRICT_SOURCE_ROUTING: {
            length = stream.readByte();
            uint8_t pointer = stream.readByte();
            if (length > 3 && (length % 4) == 3 && pointer >= 4 && (pointer % 4) == 0) {
                auto *option = new IPv4OptionRecordRoute();
                option->setType(type);
                option->setLength(length);
                option->setRecordAddressArraySize((length - 3) / 4);
                option->setNextAddressIdx((pointer-4) / 4);
                for (unsigned int count = 0; count < option->getRecordAddressArraySize(); count++) {
                    option->setRecordAddress(count, stream.readIPv4Address());
                }
                return option;
            }
            break;
        }

        case IPOPTION_ROUTER_ALERT:
        case IPOPTION_SECURITY:
        default:
            length = stream.readByte();
            break;
    }    // switch

    auto *option = new TLVOptionRaw();
    stream.seek(position);
    type = stream.readByte();
    length = stream.readByte();
    option->setType(type);
    option->setLength(length);
    if (length > 2)
        option->setBytesArraySize(length - 2);
    for (unsigned int i = 2; i < length; i++)
        option->setBytes(i-2, stream.readByte());
    return option;
}

} // namespace serializer

} // namespace inet

