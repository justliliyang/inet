//
// Copyright (C) 2014 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#ifndef __INET_APSKPHYHEADERSERIALIZER_H
#define __INET_APSKPHYHEADERSERIALIZER_H

#include "inet/common/BitVector.h"
#include "inet/common/packet/Serializer.h"
#include "inet/physicallayer/apskradio/packetlevel/APSKPhyHeader_m.h"

namespace inet {

namespace physicallayer {

#define APSK_PHY_HEADER_BYTE_LENGTH    6

class INET_API APSKPhyHeaderSerializer : public ChunkSerializer
{
  public:
    virtual void serialize(ByteOutputStream& stream, const std::shared_ptr<Chunk>& chunk, int64_t offset, int64_t length) const;
    virtual std::shared_ptr<Chunk> deserialize(ByteInputStream& stream, const std::type_info& typeInfo) const;
};

} // namespace physicallayer

} // namespace inet

#endif // ifndef __INET_APSKPHYHEADERSERIALIZER_H

