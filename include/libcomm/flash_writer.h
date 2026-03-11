#pragma once

#include <cstddef>
#include <cstdint>

namespace libcomm {

class IFlashWriter {
public:
    virtual ~IFlashWriter() = default;
    virtual bool begin(std::uint32_t firmwareSize) = 0;
    virtual bool writeChunk(std::uint16_t index, const std::uint8_t* data, std::size_t len) = 0;
    virtual bool finish() = 0;
    virtual bool verify(std::uint32_t expectedCrc32) = 0;
    virtual bool activate() = 0;
    virtual void abort() = 0;
};

}  // namespace libcomm
