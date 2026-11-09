// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#include "utils/file/source_fingerprint.hpp"

#include <gtest/gtest.h>

#include <QTemporaryDir>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Psapi requires the Win32 types before its declarations.
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on
#endif

namespace alcedo {
TEST(SourceFingerprintTest, StreamingReadMatchesTheKnownSha256OfAbcAndItsByteCount) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const image_path_t path = std::filesystem::path(dir.path().toStdWString()) / "source";
  {
    std::ofstream(path, std::ios::binary) << "abc";
  }
  SourceFingerprintReader reader(path);
  EXPECT_EQ(reader.Size(), 3);
  const auto source = reader.Read();
  EXPECT_EQ(source.byte_size, 3);
  EXPECT_EQ(source.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_NO_THROW(reader.Validate(source));
}
TEST(SourceFingerprintTest, MissingSourceReturnsReadFailureWithoutAFabricatedDigest) {
  QTemporaryDir      dir;
  const image_path_t path = std::filesystem::path(dir.path().toStdWString()) / "absent";
  try {
    SourceFingerprintReader reader(path);
    FAIL() << "Missing file was accepted";
  } catch (const SourceReadError& error) {
    EXPECT_EQ(error.code, ImportErrorCode::READ_FAILED);
  }
}
TEST(SourceFingerprintTest, StreamingReadIncludesEveryChunkAndTheFinalPartialBuffer) {
  QTemporaryDir      dir;
  const image_path_t path = std::filesystem::path(dir.path().toStdWString()) / "source";
  {
    std::ofstream(path, std::ios::binary) << std::string(2 * 1024 * 1024 + 7, 'x');
  }
  SourceFingerprintReader reader(path);
  const auto              result = reader.Read();
  EXPECT_EQ(result.byte_size, 2 * 1024 * 1024 + 7);
  EXPECT_EQ(result.sha256, "c4a55658f242df944c879aa370c9e958506e2d4bda4994aae90592330b96f917");
}
TEST(SourceFingerprintTest, CancellationStopsTheReadWithItsActualError) {
  QTemporaryDir      dir;
  const image_path_t path = std::filesystem::path(dir.path().toStdWString()) / "source";
  {
    std::ofstream(path, std::ios::binary) << std::string(128 * 1024, 'x');
  }
  SourceFingerprintReader reader(path);
  int                     checks = 0;
  try {
    reader.Read([&] { return ++checks > 1; });
    FAIL() << "Canceled hash was accepted";
  } catch (const SourceReadError& error) {
    EXPECT_EQ(error.code, ImportErrorCode::CANCELED);
    EXPECT_EQ(checks, 2);
  }
}
TEST(SourceFingerprintTest, OpenReadProtectsTheMetadataReopenBoundaryFromReplacement) {
  QTemporaryDir      dir;
  const image_path_t path        = std::filesystem::path(dir.path().toStdWString()) / "source";
  const auto         replacement = path.parent_path() / "replacement";
  {
    std::ofstream(path, std::ios::binary) << "abc";
  }
  {
    std::ofstream(replacement, std::ios::binary) << "xyz";
  }
  SourceFingerprintReader reader(path);
  const auto              fingerprint = reader.Read();
  std::error_code         error;
  std::filesystem::remove(path, error);
#ifdef _WIN32
  EXPECT_TRUE(error);
  std::ofstream writer(path, std::ios::binary | std::ios::trunc);
  EXPECT_FALSE(writer.is_open());
  EXPECT_NO_THROW(reader.Validate(fingerprint));
#else
  ASSERT_FALSE(error);
  std::filesystem::rename(replacement, path);
  EXPECT_THROW(reader.Validate(fingerprint), SourceReadError);
#endif
}
#ifdef _WIN32
TEST(SourceFingerprintTest, StreamingFingerprintMemoryDoesNotGrowWithSourceFileSize) {
  QTemporaryDir      dir;
  const image_path_t path = std::filesystem::path(dir.path().toStdWString()) / "source";
  {
    std::ofstream(path, std::ios::binary) << 'x';
  }
  {
    SourceFingerprintReader(path).Read();
  }
  constexpr uint64_t size = 64 * 1024 * 1024;
  std::filesystem::resize_file(path, size);
  PROCESS_MEMORY_COUNTERS before{}, after{};
  ASSERT_TRUE(GetProcessMemoryInfo(GetCurrentProcess(), &before, sizeof(before)));
  EXPECT_EQ(SourceFingerprintReader(path).Read().byte_size, size);
  ASSERT_TRUE(GetProcessMemoryInfo(GetCurrentProcess(), &after, sizeof(after)));
  EXPECT_LT(after.PeakWorkingSetSize - before.PeakWorkingSetSize, 8 * 1024 * 1024);
  RecordProperty("source_bytes", std::to_string(size));
  RecordProperty("peak_working_set_increase_bytes",
                 std::to_string(after.PeakWorkingSetSize - before.PeakWorkingSetSize));
}
#endif
}  // namespace alcedo
