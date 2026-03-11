#include <catch2/catch_test_macros.hpp>

#include "libcomm/ota_manager.h"
#include "libcomm/flash_writer.h"
#include "ota_messages_generated.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct MockFlashWriter : public libcomm::IFlashWriter {
    bool begin_result{true};
    bool write_result{true};
    bool finish_result{true};
    bool verify_result{true};
    bool activate_result{true};

    std::uint32_t begin_size{0};
    std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> written_chunks;
    std::uint32_t verified_crc32{0};
    std::size_t begin_count{0};
    std::size_t finish_count{0};
    std::size_t verify_count{0};
    std::size_t activate_count{0};
    std::size_t abort_count{0};

    bool begin(std::uint32_t firmwareSize) override
    {
        ++begin_count;
        begin_size = firmwareSize;
        return begin_result;
    }

    bool writeChunk(std::uint16_t index, const std::uint8_t* data, std::size_t len) override
    {
        written_chunks.emplace_back(index, std::vector<std::uint8_t>(data, data + len));
        return write_result;
    }

    bool finish() override
    {
        ++finish_count;
        return finish_result;
    }

    bool verify(std::uint32_t expectedCrc32) override
    {
        ++verify_count;
        verified_crc32 = expectedCrc32;
        return verify_result;
    }

    bool activate() override
    {
        ++activate_count;
        return activate_result;
    }

    void abort() override
    {
        ++abort_count;
    }
};

struct ProgressRecord {
    midi2pwm::ota::Target target;
    midi2pwm::ota::OtaStatus status;
    std::uint16_t chunks_received;
    std::uint16_t total_chunks;
    std::string error_message;
};

struct ProgressCollector {
    std::vector<ProgressRecord> records;

    void Handle(midi2pwm::ota::Target target, midi2pwm::ota::OtaStatus status,
                std::uint16_t chunksReceived, std::uint16_t totalChunks, const char* errorMessage)
    {
        records.push_back({target, status, chunksReceived, totalChunks,
                           errorMessage ? errorMessage : ""});
    }
};

midi2pwm::ota::OtaBeginT makeBeginMsg(midi2pwm::ota::Target target, std::uint32_t size,
                                        std::uint32_t crc32, std::uint16_t totalChunks)
{
    midi2pwm::ota::OtaBeginT msg;
    msg.target = target;
    msg.firmware_size = size;
    msg.firmware_crc32 = crc32;
    msg.total_chunks = totalChunks;
    msg.version_string = "1.0.0";
    return msg;
}

flatbuffers::DetachedBuffer packBegin(const midi2pwm::ota::OtaBeginT& msg)
{
    flatbuffers::FlatBufferBuilder builder;
    auto offset = midi2pwm::ota::OtaBegin::Pack(builder, &msg);
    builder.Finish(offset);
    return builder.Release();
}

flatbuffers::DetachedBuffer packData(midi2pwm::ota::Target target, std::uint16_t index,
                                      const std::vector<std::uint8_t>& data)
{
    flatbuffers::FlatBufferBuilder builder;
    auto dataOffset = builder.CreateVector(data);
    auto msg = midi2pwm::ota::CreateOtaData(builder, target, index, dataOffset);
    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer packEnd(midi2pwm::ota::Target target)
{
    flatbuffers::FlatBufferBuilder builder;
    auto msg = midi2pwm::ota::CreateOtaEnd(builder, target);
    builder.Finish(msg);
    return builder.Release();
}

flatbuffers::DetachedBuffer packAbort(midi2pwm::ota::Target target, const char* reason)
{
    flatbuffers::FlatBufferBuilder builder;
    auto reasonOffset = reason ? builder.CreateString(reason)
                               : flatbuffers::Offset<flatbuffers::String>();
    auto msg = midi2pwm::ota::CreateOtaAbort(builder, target, reasonOffset);
    builder.Finish(msg);
    return builder.Release();
}

struct OtaManagerHarness {
    MockFlashWriter flash;
    ProgressCollector progress;
    libcomm::OtaManager manager;

    explicit OtaManagerHarness(midi2pwm::ota::Target target = midi2pwm::ota::Target::Stm32)
        : manager(flash, target,
                  libcomm::OtaManager::SendProgressCallback::create<ProgressCollector,
                      &ProgressCollector::Handle>(progress))
    {
    }

    void sendBegin(std::uint32_t size = 7600U, std::uint32_t crc32 = 0xDEADBEEFU,
                   std::uint16_t totalChunks = 2U)
    {
        auto beginMsg = makeBeginMsg(midi2pwm::ota::Target::Stm32, size, crc32, totalChunks);
        auto buf = packBegin(beginMsg);
        const auto* fb = flatbuffers::GetRoot<midi2pwm::ota::OtaBegin>(buf.data());
        manager.handleBegin(*fb);
    }

    void sendData(std::uint16_t index, const std::vector<std::uint8_t>& data = {0x01, 0x02, 0x03})
    {
        auto buf = packData(midi2pwm::ota::Target::Stm32, index, data);
        const auto* fb = flatbuffers::GetRoot<midi2pwm::ota::OtaData>(buf.data());
        manager.handleData(*fb);
    }

    void sendEnd()
    {
        auto buf = packEnd(midi2pwm::ota::Target::Stm32);
        const auto* fb = flatbuffers::GetRoot<midi2pwm::ota::OtaEnd>(buf.data());
        manager.handleEnd(*fb);
    }

    void sendAbort(const char* reason = "cancelled")
    {
        auto buf = packAbort(midi2pwm::ota::Target::Stm32, reason);
        const auto* fb = flatbuffers::GetRoot<midi2pwm::ota::OtaAbort>(buf.data());
        manager.handleAbort(*fb);
    }
};

} // namespace

TEST_CASE("OtaManager starts in Idle state", "[ota_manager]")
{
    OtaManagerHarness h;
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
}

TEST_CASE("OtaManager happy path: Begin -> Data -> End", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(7600U, 0xDEADBEEFU, 2U);
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Receiving);
    CHECK(h.flash.begin_count == 1);
    CHECK(h.flash.begin_size == 7600U);

    h.sendData(0);
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Receiving);
    REQUIRE(h.flash.written_chunks.size() == 1);
    CHECK(h.flash.written_chunks[0].first == 0);

    h.sendData(1);
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Receiving);
    REQUIRE(h.flash.written_chunks.size() == 2);

    h.sendEnd();
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Rebooting);
    CHECK(h.flash.finish_count == 1);
    CHECK(h.flash.verify_count == 1);
    CHECK(h.flash.verified_crc32 == 0xDEADBEEFU);
    CHECK(h.flash.activate_count == 1);
}

TEST_CASE("OtaManager progress callbacks are sent during happy path", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(3800U, 0U, 1U);
    h.sendData(0);
    h.sendEnd();

    REQUIRE(h.progress.records.size() >= 4);
    CHECK(h.progress.records[0].status == midi2pwm::ota::OtaStatus::Preparing);
    CHECK(h.progress.records[1].status == midi2pwm::ota::OtaStatus::Receiving);
    CHECK(h.progress.records[2].status == midi2pwm::ota::OtaStatus::Receiving);

    bool found_verifying = false;
    bool found_applying = false;
    bool found_rebooting = false;
    for (const auto& r : h.progress.records) {
        if (r.status == midi2pwm::ota::OtaStatus::Verifying) found_verifying = true;
        if (r.status == midi2pwm::ota::OtaStatus::Applying) found_applying = true;
        if (r.status == midi2pwm::ota::OtaStatus::Rebooting) found_rebooting = true;
    }
    CHECK(found_verifying);
    CHECK(found_applying);
    CHECK(found_rebooting);
}

TEST_CASE("OtaManager rejects OtaBegin when not idle", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(3800U, 0U, 1U);
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Receiving);

    h.sendBegin(3800U, 0U, 1U);

    bool found_error = false;
    for (const auto& r : h.progress.records) {
        if (r.status == midi2pwm::ota::OtaStatus::Error &&
            r.error_message.find("not idle") != std::string::npos) {
            found_error = true;
        }
    }
    CHECK(found_error);
}

TEST_CASE("OtaManager handles flash erase failure", "[ota_manager]")
{
    OtaManagerHarness h;
    h.flash.begin_result = false;

    h.sendBegin();
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);

    bool found_error = false;
    for (const auto& r : h.progress.records) {
        if (r.status == midi2pwm::ota::OtaStatus::Error &&
            r.error_message.find("erase") != std::string::npos) {
            found_error = true;
        }
    }
    CHECK(found_error);
}

TEST_CASE("OtaManager handles flash write failure", "[ota_manager]")
{
    OtaManagerHarness h;
    h.flash.write_result = false;

    h.sendBegin(3800U, 0U, 1U);
    h.sendData(0);

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);
}

TEST_CASE("OtaManager rejects out-of-order chunks", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(7600U, 0U, 2U);
    h.sendData(1);

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);
}

TEST_CASE("OtaManager rejects OtaData when not receiving", "[ota_manager]")
{
    OtaManagerHarness h;
    std::size_t progress_before = h.progress.records.size();

    h.sendData(0);

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.progress.records.size() == progress_before);
}

TEST_CASE("OtaManager rejects OtaEnd when not receiving", "[ota_manager]")
{
    OtaManagerHarness h;
    std::size_t progress_before = h.progress.records.size();

    h.sendEnd();

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.progress.records.size() == progress_before);
}

TEST_CASE("OtaManager rejects OtaEnd with chunk count mismatch", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(7600U, 0U, 2U);
    h.sendData(0);
    h.sendEnd();

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);
}

TEST_CASE("OtaManager handles CRC verification failure", "[ota_manager]")
{
    OtaManagerHarness h;
    h.flash.verify_result = false;

    h.sendBegin(3800U, 0xDEADBEEFU, 1U);
    h.sendData(0);
    h.sendEnd();

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.verify_count == 1);
    CHECK(h.flash.abort_count == 1);
}

TEST_CASE("OtaManager handles activation failure", "[ota_manager]")
{
    OtaManagerHarness h;
    h.flash.activate_result = false;

    h.sendBegin(3800U, 0U, 1U);
    h.sendData(0);
    h.sendEnd();

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.activate_count == 1);
    CHECK(h.flash.abort_count == 1);
}

TEST_CASE("OtaManager handles finalization failure", "[ota_manager]")
{
    OtaManagerHarness h;
    h.flash.finish_result = false;

    h.sendBegin(3800U, 0U, 1U);
    h.sendData(0);
    h.sendEnd();

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);
    CHECK(h.flash.verify_count == 0);
}

TEST_CASE("OtaManager handles abort during transfer", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(7600U, 0U, 2U);
    h.sendData(0);
    h.sendAbort("User cancelled");

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);
}

TEST_CASE("OtaManager ignores abort in idle state", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendAbort("spurious");

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 0);
}

TEST_CASE("OtaManager can restart after error", "[ota_manager]")
{
    OtaManagerHarness h;
    h.flash.begin_result = false;

    h.sendBegin();
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);

    h.flash.begin_result = true;
    h.sendBegin(3800U, 0U, 1U);
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Receiving);
}

TEST_CASE("OtaManager can restart after abort", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(7600U, 0U, 2U);
    h.sendAbort();
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);

    h.sendBegin(3800U, 0U, 1U);
    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Receiving);
}

TEST_CASE("OtaManager rejects empty chunk data", "[ota_manager]")
{
    OtaManagerHarness h;

    h.sendBegin(3800U, 0U, 1U);

    flatbuffers::FlatBufferBuilder builder;
    auto msg = midi2pwm::ota::CreateOtaData(builder, midi2pwm::ota::Target::Stm32, 0);
    builder.Finish(msg);
    auto buf = builder.Release();
    const auto* fb = flatbuffers::GetRoot<midi2pwm::ota::OtaData>(buf.data());
    h.manager.handleData(*fb);

    CHECK(h.manager.status() == midi2pwm::ota::OtaStatus::Idle);
    CHECK(h.flash.abort_count == 1);
}
