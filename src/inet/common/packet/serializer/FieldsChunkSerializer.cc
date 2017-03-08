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

#include "inet/common/packet/chunk/FieldsChunk.h"
#include "inet/common/packet/serializer/FieldsChunkSerializer.h"
#include "inet/common/packet/serializer/ChunkSerializerRegistry.h"

namespace inet {

void FieldsChunkSerializer::serialize(ByteOutputStream& stream, const std::shared_ptr<Chunk>& chunk, bit offset, bit length) const
{
    auto fieldsChunk = std::static_pointer_cast<FieldsChunk>(chunk);
    if (fieldsChunk->getSerializedBytes() != nullptr)
        stream.writeBytes(*fieldsChunk->getSerializedBytes(), byte(offset).get(), length == bit(-1) ? -1 : byte(length).get());
    else if (offset == bit(0) && (length == bit(-1) || length == chunk->getChunkLength())) {
        auto streamPosition = stream.getPosition();
        serialize(stream, fieldsChunk);
        bit serializedLength = byte(stream.getPosition() - streamPosition);
        ChunkSerializer::totalSerializedBitCount += serializedLength;
        fieldsChunk->setSerializedBytes(stream.copyBytes(streamPosition, byte(serializedLength).get()));
    }
    else {
        ByteOutputStream chunkStream;
        serialize(chunkStream, fieldsChunk);
        stream.writeBytes(chunkStream.getBytes(), byte(offset).get(), length == bit(-1) ? -1 : byte(length).get());
        ChunkSerializer::totalSerializedBitCount += byte(chunkStream.getSize());
        fieldsChunk->setSerializedBytes(chunkStream.copyBytes());
    }
}

std::shared_ptr<Chunk> FieldsChunkSerializer::deserialize(ByteInputStream& stream, const std::type_info& typeInfo) const
{
    auto streamPosition = stream.getPosition();
    auto fieldsChunk = std::static_pointer_cast<FieldsChunk>(deserialize(stream));
    auto length = byte(stream.getPosition() - streamPosition);
    ChunkSerializer::totalDeserializedBitCount += length;
    fieldsChunk->setSerializedBytes(stream.copyBytes(streamPosition, byte(length).get()));
    return fieldsChunk;
}

} // namespace
