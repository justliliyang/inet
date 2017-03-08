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

#ifndef __INET_CPACKETCHUNK_H_
#define __INET_CPACKETCHUNK_H_

#include "inet/common/packet/Chunk.h"

namespace inet {

class cPacketChunk : public Chunk
{
  protected:
    cPacket *packet = nullptr;

  public:
    cPacketChunk(const cPacketChunk& other);
    cPacketChunk(cPacket *packet);
    ~cPacketChunk();

    virtual cPacketChunk *dup() const override { return new cPacketChunk(*this); }
    virtual std::shared_ptr<Chunk> dupShared() const override { return std::make_shared<cPacketChunk>(*this); }

    virtual int64_t getByteLength() const override { return packet->getByteLength(); }

    virtual cPacket *getPacket() const { return packet; }       /// do not change, do not delete returned packet, the Chunk is the owner !!!!

    virtual std::string str() const override;
};

} // namespace

#endif // #ifndef __INET_CPACKETCHUNK_H_

