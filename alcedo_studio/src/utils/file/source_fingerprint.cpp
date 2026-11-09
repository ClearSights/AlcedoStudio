// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#include "utils/file/source_fingerprint.hpp"

#include <sys/stat.h>

#include <QCryptographicHash>
#include <array>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace alcedo {
SourceFingerprintReader::SourceFingerprintReader(const image_path_t& path) : path_(path) {
#ifdef _WIN32
  file_ = _wfsopen(path.c_str(), L"rb", _SH_DENYWR);
#else
  file_ = std::fopen(path.c_str(), "rb");
#endif
  if (!file_) throw SourceReadError(ImportErrorCode::READ_FAILED, std::strerror(errno));
  try {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file_))),
                                    &info)) {
      throw SourceReadError(ImportErrorCode::READ_FAILED, "Cannot inspect source handle");
    }
    size_   = (uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
    device_ = info.dwVolumeSerialNumber;
    inode_  = (uint64_t{info.nFileIndexHigh} << 32) | info.nFileIndexLow;
#else
    struct stat info{};
    if (fstat(fileno(file_), &info) != 0 || !S_ISREG(info.st_mode)) {
      throw SourceReadError(ImportErrorCode::READ_FAILED, "Cannot inspect regular source file");
    }
    size_   = info.st_size;
    device_ = info.st_dev;
    inode_  = info.st_ino;
#ifdef __APPLE__
    modified_seconds_     = info.st_mtimespec.tv_sec;
    modified_nanoseconds_ = info.st_mtimespec.tv_nsec;
    changed_seconds_      = info.st_ctimespec.tv_sec;
    changed_nanoseconds_  = info.st_ctimespec.tv_nsec;
#else
    modified_seconds_     = info.st_mtim.tv_sec;
    modified_nanoseconds_ = info.st_mtim.tv_nsec;
    changed_seconds_      = info.st_ctim.tv_sec;
    changed_nanoseconds_  = info.st_ctim.tv_nsec;
#endif
#endif
  } catch (...) {
    std::fclose(file_);
    throw;
  }
}

SourceFingerprintReader::~SourceFingerprintReader() { std::fclose(file_); }

auto SourceFingerprintReader::Read(const std::function<bool()>& canceled) -> SourceFingerprint {
  std::rewind(file_);
  QCryptographicHash          hash(QCryptographicHash::Sha256);
  std::array<char, 64 * 1024> buffer;
  uint64_t                    count = 0;
  for (;;) {
    if (canceled && canceled()) {
      throw SourceReadError(ImportErrorCode::CANCELED, "Import canceled while reading source");
    }
    const auto read = std::fread(buffer.data(), 1, buffer.size(), file_);
    count += read;
    hash.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(read)));
    if (read < buffer.size()) {
      if (std::ferror(file_)) {
        throw SourceReadError(ImportErrorCode::READ_FAILED, "Source read failed");
      }
      break;
    }
  }
  if (count != size_) {
    throw SourceReadError(ImportErrorCode::SOURCE_CHANGED, "Source size changed during import");
  }
  return {count, hash.result().toHex().toStdString()};
}

void SourceFingerprintReader::Validate(const SourceFingerprint& expected) {
#ifndef _WIN32
  struct stat info{};
  bool        changed = stat(path_.c_str(), &info) != 0 || uint64_t(info.st_dev) != device_ ||
                        uint64_t(info.st_ino) != inode_ || uint64_t(info.st_size) != size_;
#ifdef __APPLE__
  changed = changed || info.st_mtimespec.tv_sec != modified_seconds_ ||
            info.st_mtimespec.tv_nsec != modified_nanoseconds_ ||
            info.st_ctimespec.tv_sec != changed_seconds_ ||
            info.st_ctimespec.tv_nsec != changed_nanoseconds_;
#else
  changed = changed || info.st_mtim.tv_sec != modified_seconds_ ||
            info.st_mtim.tv_nsec != modified_nanoseconds_ ||
            info.st_ctim.tv_sec != changed_seconds_ || info.st_ctim.tv_nsec != changed_nanoseconds_;
#endif
  if (changed || Read() != expected) {
    throw SourceReadError(ImportErrorCode::SOURCE_CHANGED, "Source changed during import");
  }
#else
  // The CRT read handle uses FILE_SHARE_READ only: writers, renames and deletion cannot
  // succeed while this reader is alive, including across the metadata reader's reopen.
  (void)expected;
#endif
}
}  // namespace alcedo
