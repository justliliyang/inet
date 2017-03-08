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

#ifndef __INET_BYTEARRAYCHUNK_H_
#define __INET_BYTEARRAYCHUNK_H_

#include "Chunk.h"

namespace inet {

class ByteArrayChunk : public Chunk
{
  friend Chunk;

  protected:
    std::vector<uint8_t> bytes;

  protected:
    virtual const char *getSerializerClassName() const override { return "inet::ByteArrayChunkSerializer"; }

  protected:
    static std::shared_ptr<Chunk> createChunk(const std::type_info& typeInfo, const std::shared_ptr<Chunk>& chunk, int64_t byteOffset, int64_t byteLength);

  public:
    ByteArrayChunk() { }
    ByteArrayChunk(const std::vector<uint8_t>& bytes);

    virtual ByteArrayChunk *dup() const override { return new ByteArrayChunk(*this); }
    virtual std::shared_ptr<Chunk> dupShared() const override { return std::make_shared<ByteArrayChunk>(*this); }

    virtual const std::vector<uint8_t>& getBytes() const { return bytes; }
    virtual void setBytes(const std::vector<uint8_t>& bytes);

    virtual int64_t getByteLength() const override { return bytes.size(); }

    virtual bool insertToBeginning(const std::shared_ptr<Chunk>& chunk) override;
    virtual bool insertToEnd(const std::shared_ptr<Chunk>& chunk) override;

    virtual bool removeFromBeginning(int64_t byteLength) override;
    virtual bool removeFromEnd(int64_t byteLength) override;

    // TODO: remove
    virtual std::shared_ptr<Chunk> peek(int64_t byteOffset = 0, int64_t byteLength = -1) const override {
        return peek(Iterator(shared_from_this(), true, 0, -1), byteLength);
    }

    virtual std::shared_ptr<Chunk> peek(const Iterator& iterator, int64_t byteLength = -1) const override;

    virtual std::string str() const override;
};

} // namespace

#endif // #ifndef __INET_BYTEARRAYCHUNK_H_

