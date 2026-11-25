/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2025 by Kisara Development LLC.
 * All rights reserved.
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ES40_SNAPSHOT_FILE_H
#define ES40_SNAPSHOT_FILE_H

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

// The destination is replaced only after a complete, flushed and closed save.
class CSnapshotFile
{
public:
  explicit CSnapshotFile(const char* destination) : destination_(destination)
  {
    static std::atomic<unsigned long> sequence{0};
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
#if defined(_WIN32)
      const auto process = _getpid();
#else
      const auto process = getpid();
#endif
      temporary_ = destination_ + ".tmp-" + std::to_string(process) + "-" +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
#if defined(_WIN32)
      int fd = _open(temporary_.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
        _S_IREAD | _S_IWRITE);
#else
      int fd = open(temporary_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
      if (fd < 0)
      {
        if (errno == EEXIST) continue;
        FAILURE_2(Runtime, "Can't create temporary state file %s: %s", temporary_.c_str(), std::strerror(errno));
      }
#if defined(_WIN32)
      file_ = _fdopen(fd, "wb");
#else
      file_ = fdopen(fd, "wb");
#endif
      if (!file_)
      {
#if defined(_WIN32)
        _close(fd);
#else
        close(fd);
#endif
        std::remove(temporary_.c_str());
        FAILURE(Runtime, "Can't open temporary state stream");
      }
      return;
    }
    FAILURE(Runtime, "Can't allocate a unique temporary state file");
  }

  ~CSnapshotFile()
  {
    if (file_) fclose(file_);
    if (!temporary_.empty()) std::remove(temporary_.c_str());
  }
  CSnapshotFile(const CSnapshotFile&) = delete;
  CSnapshotFile& operator=(const CSnapshotFile&) = delete;
  FILE* get() const { return file_; }

  void publish()
  {
    if (ferror(file_) || fflush(file_) != 0)
      FAILURE(Runtime, "Unable to flush system state");
#if defined(_WIN32)
    if (_commit(_fileno(file_)) != 0)
#else
    if (fsync(fileno(file_)) != 0)
#endif
      FAILURE(Runtime, "Unable to persist system state");
    FILE* closing = file_;
    file_ = nullptr;
    if (fclose(closing) != 0)
      FAILURE(Runtime, "Unable to finish writing system state");
#if defined(_WIN32)
    if (!MoveFileExA(temporary_.c_str(), destination_.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      FAILURE_2(Runtime, "Unable to publish state file %s (Windows error %lu)",
        destination_.c_str(), GetLastError());
#else
    if (std::rename(temporary_.c_str(), destination_.c_str()) != 0)
      FAILURE_1(Runtime, "Unable to publish state file %s", destination_.c_str());
#endif
    temporary_.clear();
  }

private:
  std::string destination_;
  std::string temporary_;
  FILE* file_ = nullptr;
};

#endif
