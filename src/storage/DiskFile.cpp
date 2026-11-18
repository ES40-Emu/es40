/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2026 by GenSayer
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

/**
 * \file
 * Contains code to use a file as a disk image.
 *
 * $Id$
 *
 * X-1.23       Added BIN/CUE support.
 *              - Transparent: ISO/raw images and all existing OpenVMS disk
 *                images continue to work with zero code-path changes.
 *              - Activated automatically when filename ends in .cue
 *                (case-insensitive comparison, works on VMS/Win/Unix).
 *              - Supports single- and multi-file BIN/CUE layouts,
 *                MODE1/2048, MODE1/2352, MODE2/2336, MODE2/2352, AUDIO.
 *              - Falls back to raw-image handling if .cue parsing fails.
 *              - Multi-track TOC exposed via get_track() / get_track_for_lba()
 *                for use by the SCSI READ TOC handler in Disk.cpp.
 *
 * X-1.22       Camiel Vanderhoeven                             26-MAR-2008
 *      Support OpenVMS path names.
 *
 * X-1.21       Camiel Vanderhoeven                             14-MAR-2008
 *      Formatting.
 *
 * X-1.20       Camiel Vanderhoeven                             19-MAR-2008
 *      Use fopen_large to support files >2GB on linux.
 *
 * X-1.19       Camiel Vanderhoeven                             14-MAR-2008
 *   1. More meaningful exceptions replace throwing (int) 1.
 *   2. U64 macro replaces X64 macro.
 *
 * X-1.18       Camiel Vanderhoeven                             05-MAR-2008
 *      Multi-threading version.
 *
 * X-1.17       Camiel Vanderhoeven                             02-MAR-2008
 *      Natural way to specify large numeric values ("10M") in config file.
 *
 * X-1.16       David Leonard                                   20-FEB-2008
 *      Show disk creation progress.
 *
 * X-1.15       Camiel Vanderhoeven                             25-JAN-2008
 *      Create file if it doesn't exist and autocreate_size is specified.
 *
 * X-1.14       Camiel Vanderhoeven                             13-JAN-2008
 *      Use determine_layout instead of calc_cylinders.
 *
 * X-1.13       Brian Wheeler                                   09-JAN-2008
 *      Put filename in disk model number (without path).
 *
 * X-1.12       Camiel Vanderhoeven                             09-JAN-2008
 *      Save disk state to state file.
 *
 * X-1.11       Camiel Vanderhoeven                             06-JAN-2008
 *      Set default blocksize to 2048 for cd-rom devices.
 *
 * X-1.10       Camiel Vanderhoeven                             06-JAN-2008
 *      Support changing the block size (required for SCSI, ATAPI).
 *
 * X-1.9        Camiel Vanderhoeven                             04-JAN-2008
 *      64-bit file I/O.
 *
 * X-1.8        Camiel Vanderhoeven                             02-JAN-2008
 *      Cleanup.
 *
 * X-1.7        Camiel Vanderhoeven                             28-DEC-2007
 *      Throw exceptions rather than just exiting when errors occur.
 *
 * X-1.6        Camiel Vanderhoeven                             28-DEC-2007
 *      Keep the compiler happy.
 *
 * X-1.5        Camiel Vanderhoeven                             20-DEC-2007
 *      Close files and free memory when the emulator shuts down.
 *
 * X-1.4        Camiel Vanderhoeven                             18-DEC-2007
 *      Byte-sized transfers for SCSI controller.
 *
 * X-1.3        Brian Wheeler                                   17-DEC-2007
 *      Changed last cylinder number.
 *
 * X-1.2        Brian Wheeler                                   16-DEC-2007
 *      Fixed case of StdAfx.h.
 *
 * X-1.1        Camiel Vanderhoeven                             12-DEC-2007
 *      Initial version in CVS.
 **/

#include "StdAfx.h"
#include "DiskFile.h"

#include <ctype.h>
#include <exception>
#include <filesystem>
#include <string>
#include <string.h>
#include <cerrno>
#include <climits>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{
std::mutex removable_disks_mutex;
std::vector<std::shared_ptr<CDiskFileMediaMailbox> > removable_disks;

void register_removable_disk(
    const std::shared_ptr<CDiskFileMediaMailbox>& mailbox) noexcept
{
    try
    {
        if (!mailbox)
            return;
        std::lock_guard<std::mutex> lock(removable_disks_mutex);
        removable_disks.push_back(mailbox);
    }
    catch (...)
    {
    }
}

void unregister_removable_disk(
    const std::shared_ptr<CDiskFileMediaMailbox>& mailbox) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(removable_disks_mutex);
        for (std::vector<std::shared_ptr<CDiskFileMediaMailbox> >::iterator it =
                 removable_disks.begin(); it != removable_disks.end(); ++it)
        {
            if (*it == mailbox)
            {
                removable_disks.erase(it);
                break;
            }
        }
    }
    catch (...)
    {
    }
}

// Length-prefix paths so arbitrary filename characters cannot alias fields.
void append_identity_string(std::ostringstream& out, const std::string& value)
{
    out << value.size() << ':' << value << ';';
}

std::string image_identity(FILE* image, const std::string& path)
{
    if (!image)
        FAILURE(Runtime, "Snapshot image has no open backing file");
    std::ostringstream out;
#if defined(_WIN32)
    const HANDLE native = (HANDLE)_get_osfhandle(_fileno(image));
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(native, &info))
        FAILURE_1(Runtime, "Cannot identify snapshot image: %s", path.c_str());
    const DWORD path_flags = FILE_NAME_NORMALIZED;
    const DWORD needed = GetFinalPathNameByHandleA(native, nullptr, 0, path_flags);
    if (!needed)
        FAILURE_2(Runtime, "Cannot resolve snapshot image %s (error %lu)",
                  path.c_str(), GetLastError());
    if (needed == MAXDWORD)
        FAILURE(Runtime, "Snapshot image path is too long");
    std::string canonical(static_cast<size_t>(needed) + 1, '\0');
    const DWORD length = GetFinalPathNameByHandleA(native, &canonical[0],
        static_cast<DWORD>(canonical.size()), path_flags);
    if (!length || length >= canonical.size())
        FAILURE_1(Runtime, "Cannot resolve snapshot image: %s", path.c_str());
    canonical.resize(length);

    // The mounted handle and its pathname must still refer to the same file.
    // IF not, we end up opening a replacement image.... whoops. 
    const HANDLE named = CreateFileA(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (named == INVALID_HANDLE_VALUE)
        FAILURE_1(Runtime, "Cannot reopen snapshot image identity: %s", path.c_str());
    BY_HANDLE_FILE_INFORMATION named_info;
    const BOOL identified = GetFileInformationByHandle(named, &named_info);
    const BOOL closed = CloseHandle(named);
    if (!identified || !closed ||
        info.dwVolumeSerialNumber != named_info.dwVolumeSerialNumber ||
        info.nFileIndexHigh != named_info.nFileIndexHigh ||
        info.nFileIndexLow != named_info.nFileIndexLow)
        FAILURE_1(Runtime, "Snapshot image pathname was replaced: %s", path.c_str());
    append_identity_string(out, canonical);
    out << info.dwVolumeSerialNumber << ':'
        << info.nFileIndexHigh << ':' << info.nFileIndexLow << ':'
        << info.nFileSizeHigh << ':' << info.nFileSizeLow << ':'
        << info.ftLastWriteTime.dwHighDateTime << ':'
        << info.ftLastWriteTime.dwLowDateTime;
#else
    struct stat info;
    if (fstat(fileno(image), &info) != 0)
        FAILURE_1(Runtime, "Cannot identify snapshot image: %s", path.c_str());
    std::unique_ptr<char, decltype(&free)> canonical(
        realpath(path.c_str(), nullptr), &free);
    if (!canonical)
        FAILURE_1(Runtime, "Cannot resolve snapshot image: %s", path.c_str());
    struct stat named_info;
    if (stat(canonical.get(), &named_info) != 0 ||
        info.st_dev != named_info.st_dev || info.st_ino != named_info.st_ino)
        FAILURE_1(Runtime, "Snapshot image pathname was replaced: %s", path.c_str());
    append_identity_string(out, canonical.get());
    out << info.st_dev << ':' << info.st_ino << ':' << info.st_size << ':'
        << info.st_mtime;
#if defined(__APPLE__)
    out << ':' << info.st_mtimespec.tv_nsec;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
    out << ':' << info.st_mtim.tv_nsec;
#endif
#endif
    return out.str();
}
}

std::vector<std::shared_ptr<CDiskFileMediaMailbox> > disk_file_removable_media()
{
    std::lock_guard<std::mutex> lock(removable_disks_mutex);
    return removable_disks;
}

CDiskFileMediaMailbox::CDiskFileMediaMailbox(const std::string& label,
    bool floppy, bool initial_read_only) :
    device_label(label), floppy_device(floppy),
    read_only(initial_read_only), guest_locked(false), active(true),
    actions_pending(false)
{
}

bool CDiskFileMediaMailbox::request_image_change(
    const char* path, bool force_locked) noexcept
{
    try
    {
        if (!path || !*path)
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (!active)
            return false;
        pending_actions.push_back(
            { EDiskFileMediaAction::ChangeImage, path, 0, false,
              force_locked });
        actions_pending.store(true, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool CDiskFileMediaMailbox::request_blank_floppy(
    const char* path, size_t image_size) noexcept
{
    try
    {
        if (!path || !*path || image_size == 0 || image_size % 512 != 0)
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (!active || !floppy_device)
            return false;
        pending_actions.push_back(
            { EDiskFileMediaAction::CreateBlankFloppy, path, image_size,
              false, false });
        actions_pending.store(true, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool CDiskFileMediaMailbox::request_eject(bool force_locked) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active)
            return false;
        pending_actions.push_back(
            { EDiskFileMediaAction::Eject, std::string(), 0, false,
              force_locked });
        actions_pending.store(true, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool CDiskFileMediaMailbox::request_read_only_toggle() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active || !floppy_device)
            return false;

        // Queue absolute state rather than relative toggle. 
        // Repeated requests remain deterministic regardless of FDC state.
        const bool desired = !read_only.load(std::memory_order_relaxed);
        pending_actions.push_back(
            { EDiskFileMediaAction::SetReadOnly, std::string(), 0,
              desired, false });
        actions_pending.store(true, std::memory_order_release);
        read_only.store(desired, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool CDiskFileMediaMailbox::take_pending_actions(
    std::deque<SDiskFileMediaAction>& actions) noexcept
{
    try
    {
        if (!actions_pending.load(std::memory_order_acquire))
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (!active || pending_actions.empty())
            return false;
        actions.swap(pending_actions);
        // Callers normally supply an empty deque. 
        // Keep the flag accurate if the swap instead leaves their previous contents.
        actions_pending.store(!pending_actions.empty(), std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void CDiskFileMediaMailbox::reconcile_read_only(bool actual_value) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool projected_value = actual_value;
        for (std::deque<SDiskFileMediaAction>::const_iterator it =
                 pending_actions.begin(); it != pending_actions.end(); ++it)
            if (it->type == EDiskFileMediaAction::SetReadOnly)
                projected_value = it->read_only;
        read_only.store(projected_value, std::memory_order_release);
    }
    catch (...)
    {
    }
}

void CDiskFileMediaMailbox::update_mounted_image(
    const std::string& path) noexcept
{
    try
    {
        std::string image;
        if (!path.empty())
        {
            const std::string::size_type separator =
                path.find_last_of("/\\]:");
            image = separator == std::string::npos ?
                path : path.substr(separator + 1);
        }

        std::lock_guard<std::mutex> lock(mutex);
        mounted_image.swap(image);
    }
    catch (...)
    {
    }
}

std::string CDiskFileMediaMailbox::mounted_image_name() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return mounted_image.empty() ? "<none>" : mounted_image;
}

void CDiskFileMediaMailbox::deactivate() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(mutex);
        active = false;
        pending_actions.clear();
        actions_pending.store(false, std::memory_order_relaxed);
    }
    catch (...)
    {
    }
}

// ===========================================================================
//  CDiskFile: constructor / destructor
// ===========================================================================

CDiskFile::CDiskFile(CConfigurator* cfg, CSystem* sys, CDiskController* c,
    int idebus, int idedev) : CDisk(cfg, sys, c, idebus, idedev)
{
    // Initialise bin/cue state before anything else so that the destructor
    // is always safe to call even if the constructor throws.
    is_bincue        = false;
    tracks           = nullptr;
    track_count      = 0;
    current_lba      = 0;
    logical_byte_pos = 0;
    floppy_device    = myCfg->get_myParent() != nullptr &&
                        myCfg->get_myParent()->get_class_id() == c_floppy;
    char* configured_filename = myCfg->get_text_value("file");
    const char* overlay_filename = myCfg->get_text_value("overlay_file", "");
    if (*overlay_filename && (floppy_device || cdrom() ||
        !configured_filename || !*configured_filename))
        FAILURE(Configuration, "overlay_file requires a fixed hard disk with a base file");
    const bool start_without_media = (floppy_device || cdrom()) &&
                                     (!configured_filename ||
                                      !*configured_filename);
    if (!configured_filename && !start_without_media)
    {
        FAILURE_1(Configuration, "%s: Disk has no file attached!\n", devid_string);
    }

    if (!start_without_media && !reload_file(*overlay_filename ? overlay_filename : configured_filename))
        FAILURE_1(Runtime, "%s: Could not mount configured disk image", devid_string);
    state.scsi.media_changed = 0;

    const char* default_model = floppy_device ?
        "3.5-inch 1.44 MB floppy" : "DEC RRD42 CD-ROM";
    model_number = myCfg->get_text_value("model_number",
        start_without_media ? default_model : configured_filename);

    // Advance model_number pointer past any directory component.
    char* p = model_number;
#if defined(_WIN32)
    char sep = '\\';
#elif defined(__VMS)
    char sep = ']';
#else
    char sep = '/';
#endif
    while (*p)
    {
        if (*p == sep)
            model_number = p + 1;
        p++;
    }

    if (cdrom() || floppy_device)
    {
        const char* kind = floppy_device ? "Floppy" : "CD-ROM";
        const char* device = devid_string ? devid_string : "unnamed device";
        media_mailbox = std::make_shared<CDiskFileMediaMailbox>(
            std::string(kind) + ": " + device, floppy_device, read_only);
        media_mailbox->update_mounted_image(filename);
        register_removable_disk(media_mailbox);
    }

    if (start_without_media)
        printf("%s: %s drive has no media.\n", devid_string,
               floppy_device ? "3.5-inch 1.44 MB floppy" : "CD-ROM");
    else
        printf("%s: Mounted file %s, %" PRId64 " %zu-byte blocks, "
               "%" PRId64 "/%ld/%ld.\n",
               devid_string, filename.c_str(),
               byte_size / state.block_size, state.block_size,
               cylinders, heads, sectors);
}

CDiskFile::~CDiskFile(void)
{
    printf("%s: Closing file.\n", devid_string);
    try
    {
        prepare_snapshot();
    }
    catch (const CException& error)
    {
        printf("%s: Could not flush image on close: %s\n",
               devid_string, error.message().c_str());
    }
    catch (const std::exception& error)
    {
        printf("%s: Could not flush image on close: %s\n",
               devid_string, error.what());
    }
    catch (...)
    {
        printf("%s: Could not flush image on close.\n", devid_string);
    }

    if (media_mailbox)
    {
        media_mailbox->deactivate();
        unregister_removable_disk(media_mailbox);
    }

    // Close any open bin/cue file handles and free the track array.
    if (is_bincue)
    {
        close_bin_handles();
        if (tracks)
        {
            free(tracks);
            tracks = nullptr;
        }
    }

    // Close the plain-file handle (nullptr when bin/cue is active).
    close_image(handle, filename.c_str());
}


// ===========================================================================
//  reload_file  –  entry point for both initial load and media change
// ===========================================================================

bool CDiskFile::reload_file(const char* _filename)
{
    return load_file_transactional(_filename, true, true);
}

bool CDiskFile::change_media(const char* _filename)
{
    return load_file_transactional(_filename, false, false);
}

bool CDiskFile::create_blank_floppy(const char* _filename,
                                    size_t image_size)
{
    if (!floppy_device || !_filename || !*_filename)
        return false;

    switch (image_size)
    {
    case  360 * 1024:
    case  720 * 1024:
    case 1200 * 1024:
    case 1440 * 1024:
    case 2880 * 1024:
        break;
    default:
        printf("%s: Unsupported blank floppy image size: %zu bytes.\n",
               devid_string, image_size);
        return false;
    }

    bool same_image = filename == _filename;
#if defined(_WIN32)
    same_image = _stricmp(filename.c_str(), _filename) == 0;
#endif
    if (same_image)
    {
        printf("%s: Refusing to replace the currently mounted floppy image.\n",
               devid_string);
        return false;
    }

    FILE* blank = fopen_large(_filename, "wb");
    if (!blank)
    {
        printf("%s: Could not create blank floppy image %s.\n",
               devid_string, _filename);
        return false;
    }

    const bool written =
        fseek_large(blank, (off_t_large)image_size - 1, SEEK_SET) == 0 &&
        fputc(0, blank) != EOF && fflush(blank) == 0;
    const bool closed = fclose(blank) == 0;
    if (!written || !closed)
    {
        printf("%s: Could not finish blank floppy image %s.\n",
               devid_string, _filename);
        return false;
    }

    return true;
}

bool CDiskFile::eject_media()
{
    if (!media_present() && filename.empty())
    {
        if (media_mailbox)
            media_mailbox->update_mounted_image(filename);
        return true;
    }

    if (!flush_before_media_change())
        return false;

    reset_bincue_state();
    const bool closed = close_image(handle, filename.c_str());

    filename.clear();
    byte_size = 0;
    cylinders = 0;
    heads = 0;
    sectors = 0;
    state.byte_pos = 0;
    state.scsi.media_changed = 1;
    if (media_mailbox)
        media_mailbox->update_mounted_image(filename);
    if (!closed)
        printf("%s: Media is now ejected, but closing its image reported an error.\n",
               devid_string);
    return true;
}

void CDiskFile::media_lock_changed(bool locked)
{
    if (media_mailbox)
        media_mailbox->update_media_lock(locked);
}

bool CDiskFile::set_read_only(bool desired_read_only)
{
    if (desired_read_only == read_only)
        return true;

    // BIN/CUE media are read-only in this backend.
    if (is_bincue)
        return false;
    // Version 2 QCOW2 images remain read-only.
    if (!desired_read_only && qcow2 && qcow2->version != 3)
        return false;
    if (!handle || filename.empty())
        return false;

    if (!flush_before_media_change())
        return false;

    if (qcow2)
    {
        const std::string identity = image_identity(handle, filename);
        const bool old_read_only = read_only;
        const off_t_large old_size = byte_size;
        std::unique_ptr<Qcow2State> old_qcow2 = std::move(qcow2);
        close_image(handle, filename.c_str());
        read_only = desired_read_only;

        auto restore = [&]()
        {
            qcow2.reset();
            close_image(handle, filename.c_str());
            read_only = old_read_only;
            byte_size = old_size;
            qcow2 = std::move(old_qcow2);
            handle = open_image(filename.c_str(), read_only ? "rb" : "rb+");
            try
            {
                if (!handle || image_identity(handle, filename) != identity)
                    FAILURE_1(IO, "%s: Cannot restore image after reopening", devid_string);
            }
            catch (...)
            {
                close_image(handle, filename.c_str());
                qcow2->failed = true;
                throw;
            }
        };

        handle = open_image(filename.c_str(), read_only ? "rb" : "rb+");
        if (!handle)
        {
            restore();
            return false;
        }
        try
        {
            if (image_identity(handle, filename) != identity)
                FAILURE_1(IO, "%s: Image changed while reopening", devid_string);
            qcow2_open(filename.c_str(), old_qcow2->backing ?
                (old_qcow2->backing_is_qcow2 ? "qcow2" : "raw") : nullptr);
            if (!qcow2_same_backing(qcow2.get(), old_qcow2.get()))
                FAILURE_1(IO, "%s: Backing file changed while reopening", devid_string);
        }
        catch (...)
        {
            restore();
            throw;
        }
        return true;
    }

    FILE* replacement = fopen_large(filename.c_str(),
                                   desired_read_only ? "rb" : "rb+");
    if (!replacement)
        return false;

    if (fseek_large(replacement, state.byte_pos, SEEK_SET) != 0)
    {
        close_image(replacement, filename.c_str());
        return false;
    }

    FILE* old_handle = handle;
    handle = replacement;
    read_only = desired_read_only;
    close_image(old_handle, filename.c_str());
    return true;
}

bool CDiskFile::load_file_transactional(const char* _filename,
                                        bool allow_autocreate,
                                        bool allow_cue_fallback)
{
    if (!_filename || !*_filename)
        return false;

    if (qcow2 && handle && filename == _filename)
    {
        if (!flush_before_media_change())
            return false;
        snapshot_identity(); // Verify that the mounted paths still name these files.
        return true;
    }

    // Allocate the persistent path before touching the mounted image.
    // failure here is harmless.
    std::string new_filename(_filename);
    const bool overlay = *myCfg->get_text_value("overlay_file", "") != 0;
    if (overlay)
        _filename = myCfg->get_text_value("file", "");

    enum Format { Auto, Raw, Iso, BinCue, Qcow2 };

    const char* format_name = myCfg->get_text_value("format");

    Format requested_format;

    if (!format_name || !strcmp(format_name, "auto")) requested_format = Auto;
    else if (!strcmp(format_name, "raw")) requested_format = Raw;
    else if (!strcmp(format_name, "iso")) requested_format = Iso;
    else if (!strcmp(format_name, "bincue")) requested_format = BinCue;
    else if (!strcmp(format_name, "qcow2")) requested_format = Qcow2;
    else FAILURE_1(Configuration, "Unknown image format: %s", format_name);
    
    const char* suffix = strrchr(_filename, '.');
    const char* extension = suffix ? suffix : "";
    
    Format extension_format = Auto;
    
    if (!strcasecmp(extension, ".raw")) extension_format = Raw;
    else if (!strcasecmp(extension, ".qcow2")) extension_format = Qcow2;
    else if (cdrom() && !strcasecmp(extension, ".iso")) extension_format = Iso;
    else if (cdrom() && !strcasecmp(extension, ".bin")) extension_format = Raw;
    else if (!strcasecmp(extension, ".cue")) extension_format = BinCue;

    // CUE files are descriptors. Other extensions allow explicit auto detection.
    Format format = requested_format;
    
    if (extension_format == BinCue || (extension_format != Auto &&
        (!format_name || requested_format != Auto)))
        format = extension_format;
    const bool removable = cdrom() || floppy_device;
    if (overlay && removable)
        FAILURE(Configuration, "overlay_file requires a fixed hard disk");
    if (overlay && (format == Iso || format == BinCue))
        FAILURE(Configuration, "overlay_file requires a raw or QCOW2 base image");

    // Keep the mounted image mounted until we've opened the replacement
    // this way, a failed selection returns cleanly without changing anything
    struct MountedState
    {
        FILE* handle;
        bool is_bincue;
        CueTrack* tracks;
        int track_count;
        long current_lba;
        off_t_large logical_byte_pos;
        off_t_large byte_size;
        off_t_large cylinders;
        long heads;
        long sectors;
        off_t_large byte_pos;
        int media_changed;
        std::unique_ptr<Qcow2State> qcow2;
    } old = {
        handle, is_bincue, tracks, track_count, current_lba,
        logical_byte_pos, byte_size, cylinders, heads, sectors,
        state.byte_pos, state.scsi.media_changed, nullptr
    };

    if (!flush_before_media_change())
        return false;

    old.qcow2 = std::move(qcow2);
    handle           = nullptr;
    is_bincue        = false;
    tracks           = nullptr;
    track_count      = 0;
    current_lba      = 0;
    logical_byte_pos = 0;
    byte_size        = 0;
    state.byte_pos   = 0;

    auto discard_current = [this]()
    {
        qcow2.reset();
        reset_bincue_state();
        close_image(handle, "replacement image");
    };

    auto restore_old = [this, &old, &discard_current]()
    {
        discard_current();
        handle                   = old.handle;
        qcow2                    = std::move(old.qcow2);
        is_bincue                = old.is_bincue;
        tracks                   = old.tracks;
        track_count              = old.track_count;
        current_lba              = old.current_lba;
        logical_byte_pos         = old.logical_byte_pos;
        byte_size                = old.byte_size;
        cylinders                = old.cylinders;
        heads                    = old.heads;
        sectors                  = old.sectors;
        state.byte_pos           = old.byte_pos;
        state.scsi.media_changed = old.media_changed;
    };

    auto discard_old = [this, &old, &new_filename]()
    {
        if (old.tracks)
        {
            for (int i = 0; i < old.track_count; i++)
            {
                close_image(old.tracks[i].fileHandle, old.tracks[i].filename);
            }
            free(old.tracks);
        }
        if (!close_image(old.handle, new_filename.c_str()))
            printf("%s: Replacement image is mounted; closing the previous image reported an error.\n",
                   devid_string);
    };

    try
    {
        bool loaded = false;
        bool protected_image = false;
        bool created_qcow2 = false;

        if (format == BinCue)
        {
            printf("%s: Loading BIN/CUE image...\n",
                   devid_string);

            if (try_parse_cue(_filename))
            {
                // Expose total user-data bytes (LBAs * 2048) to SCSI.
                is_bincue = true;
                byte_size = 0;
                for (int i = 0; i < track_count; i++)
                {
                    long track_lbas = tracks[i].endLBA - tracks[i].startLBA;
                    if (track_lbas > 0)
                        byte_size += (off_t_large)track_lbas * 2048;
                }

                state.byte_pos   = 0;
                logical_byte_pos = 0;
                current_lba      = 0;
                sectors          = 32;
                heads            = 8;
                loaded = true;

                printf("%s: BIN/CUE loaded: %d track(s), %" PRId64
                       " logical bytes.\n",
                       devid_string, track_count, byte_size);
            }
            else
            {
                if (!allow_cue_fallback)
                {
                    restore_old();
                    return false;
                }

                // Preserve the fallback for raw whose names happen to end in .cue.
                printf("%s: BIN/CUE parse failed, falling back to raw image.\n",
                       devid_string);
            }
        }

        if (!loaded)
        {
            if (overlay)
            {
                handle = open_image(_filename, "rb");
                protected_image = handle != nullptr;
            }
            else if (read_only)
                handle = fopen(_filename, "rb");
            else
                handle = fopen_large(_filename, "rb+");

            // A sharing or permission failure must never trigger creation.
            if (!handle && errno != ENOENT)
                FAILURE_2(IO, "Could not open disk image %s: %s", _filename, strerror(errno));

            if (!handle && format != BinCue)
            {
                const char* create_name = myCfg->get_text_value("autocreate_format", "raw");

                Format create_format = Raw;

                if (!strcmp(create_name, "qcow2"))
                    create_format = Qcow2;
                else if (strcmp(create_name, "raw"))
                    FAILURE(Configuration, "autocreate_format must be raw or qcow2");

                if (requested_format != Auto && requested_format != create_format)
                    FAILURE(Configuration, "Missing disk: format must match autocreate_format (default raw)");

                format = create_format;

                if (format == Qcow2)
                {
                    if (removable)
                        FAILURE(Configuration, "QCOW2 images require a fixed hard disk");

                    const u64 size = allow_autocreate ? myCfg->get_num_value("autocreate_size", false, 0) : 0;

                    if (!size || (!overlay && read_only))
                        FAILURE_1(IO, "%s: QCOW2 file is missing; set autocreate_size for a writable disk to create it", _filename);

                    if (size % 512 || size > static_cast<u64>((std::numeric_limits<off_t_large>::max)()))
                        FAILURE_1(IO, "%s: QCOW2 size must be sector-aligned and fit this host's file offsets", _filename);

                    handle = open_image(_filename, "w+bx");

                    if (!handle)
                        FAILURE_2(IO, "Could not create QCOW2 image %s: %s", _filename, strerror(errno));

                    protected_image = true;
                    qcow2_create(size, "");
                    created_qcow2 = true;
                }
            }

            if (!handle)
            {
                printf("%s: Could not open file %s!\n", devid_string, _filename);

                const u64 size = allow_autocreate ?
                    myCfg->get_num_value("autocreate_size", false, 0) : 0;
                if (!size)
                {
                    restore_old();
                    return false;
                }
                if (size > static_cast<u64>((std::numeric_limits<off_t_large>::max)()))
                    FAILURE_1(Configuration, "%s: Raw image size must fit this host's file offsets", _filename);

                handle = overlay ? open_image(_filename, "w+bx") : fopen_large(_filename, "wb");
                if (!handle)
                {
                    restore_old();
                    return false;
                }

                const size_t chunk_size = 1024 * 1024;
                void* crt_buf = calloc(1, chunk_size);
                if (!crt_buf)
                {
                    restore_old();
                    return false;
                }

                printf("%s: writing %" PRIu64 " bytes:   0%%\b\b\b\b",
                       devid_string, size);

                int lastpc = 0;
                bool write_ok = true;
                for (u64 written = 0; written < size;)
                {
                    const size_t count = static_cast<size_t>((std::min)(
                        size - written, static_cast<u64>(chunk_size)));
                    if (fwrite(crt_buf, 1, count, handle) != count)
                    {
                        write_ok = false;
                        break;
                    }
                    written += count;

                    const int pc = static_cast<int>(100.0 * written / size);
                    if (pc != lastpc)
                    {
                        printf("%3d\b\b\b", pc);
                        lastpc = pc;
                    }

                    fflush(stdout);
                }

                if (!write_ok || fflush(handle) != 0)
                {
                    free(crt_buf);
                    restore_old();
                    return false;
                }

                printf("100%%\n");
                free(crt_buf);
                const std::string created_identity = overlay ? file_identity(handle) : "";
                int close_result = fclose(handle);
                handle = nullptr;
                if (close_result != 0)
                {
                    restore_old();
                    return false;
                }

                if (overlay)
                {
                    handle = open_image(_filename, "rb");
                    protected_image = handle != nullptr;
                }
                else if (read_only)
                    handle = fopen_large(_filename, "rb");
                else
                    handle = fopen_large(_filename, "rb+");

                if (!handle)
                {
                    restore_old();
                    return false;
                }
                if (overlay && file_identity(handle) != created_identity)
                    FAILURE_1(IO, "%s: Base file changed while reopening", _filename);

                printf("%s: %" PRIu64 " byte file %s created.\n",
                       devid_string, size, _filename);
            }

            if (overlay && created_qcow2)
            {
                const std::string identity = file_identity(handle);
                close_image(handle, _filename);
                handle = open_image(_filename, "rb");
                if (!handle || file_identity(handle) != identity)
                    FAILURE_1(IO, "%s: Cannot reopen created base read-only", _filename);
            }

            if (format == Auto)
                format = qcow2_probe(handle) ? Qcow2 : Raw;
            // An unrecognized header with a QCOW2 extension uses the raw fallback.
            else if (format == Qcow2 && extension_format == Qcow2 && !qcow2_probe(handle))
                format = Raw;

            if (format == Qcow2)
            {
                if (removable)
                    FAILURE(Configuration, "QCOW2 images require a fixed hard disk");
                if (!protected_image)
                {
                    close_image(handle, _filename);
                    handle = open_image(_filename, (overlay || read_only) ? "rb" : "rb+");
                    if (!handle)
                        FAILURE_2(IO, "Could not protect disk image %s: %s", _filename, strerror(errno));
                }
                if (overlay)
                {
                    qcow2 = std::make_unique<Qcow2State>();
                    qcow2_open(handle, _filename, qcow2.get(), true);
                    byte_size = static_cast<off_t_large>(qcow2->virtual_size);
                }
                else
                    qcow2_open(_filename);
            }

            if (!qcow2)
            {
                // Raw and ISO images both expose the file's bytes directly.
                if (fseek_large(handle, 0, SEEK_END) != 0)
                {
                    restore_old();
                    return false;
                }
                byte_size = ftell_large(handle);
                if (byte_size < (off_t_large)state.block_size ||
                    fseek_large(handle, 0, SEEK_SET) != 0)
                {
                    restore_old();
                    return false;
                }
                state.byte_pos = ftell_large(handle);
                if (state.byte_pos != 0)
                {
                    restore_old();
                    return false;
                }
            }

            sectors = 32;
            heads   = 8;
        }

        if (overlay)
        {
            // Keep the validated base and its chain locked until the overlay is mounted.
            std::unique_ptr<FILE, int (*)(FILE*)> backing(handle, &fclose);
            handle = nullptr;
            std::unique_ptr<Qcow2State> base_qcow2 = std::move(qcow2);
            const char* backing_format = base_qcow2 ? "qcow2" : "raw";
            const u64 size = static_cast<u64>(byte_size);
            if (!size || size % 512)
                FAILURE(Configuration, "Overlay base size must be positive and sector-aligned");

            std::error_code error;
            const std::filesystem::path base = std::filesystem::absolute(_filename, error);
            if (error)
                FAILURE_1(IO, "Cannot resolve base file: %s", error.message().c_str());
            _filename = new_filename.c_str();
            handle = open_image(_filename, read_only ? "rb" : "rb+");
            if (!handle && errno != ENOENT)
                FAILURE_2(IO, "Could not open overlay %s: %s", _filename, strerror(errno));
            if (!handle)
            {
                const std::filesystem::path child = std::filesystem::absolute(_filename, error);
                if (error)
                    FAILURE_1(IO, "Cannot resolve overlay_file: %s", error.message().c_str());
                const std::filesystem::path relative = base.lexically_relative(child.parent_path());
                const std::string backing_file = relative.empty() ? base.string() : relative.generic_string();
                if (backing_file.size() > 1023)
                    FAILURE(Configuration, "QCOW2 backing filename must not exceed 1023 bytes");
                handle = open_image(_filename, "w+bx");
                if (!handle)
                    FAILURE_2(IO, "Could not create overlay %s: %s", _filename, strerror(errno));
                qcow2_create(size, backing_file.c_str(), backing_format);
                if (read_only)
                {
                    const std::string identity = file_identity(handle);
                    close_image(handle, _filename);
                    handle = open_image(_filename, "rb");
                    if (!handle || file_identity(handle) != identity)
                        FAILURE_1(IO, "%s: Cannot reopen created overlay read-only", _filename);
                }
            }
            qcow2_open(_filename, backing_format);
            if (file_identity(backing.get()) != qcow2->backing_identity ||
                !qcow2_same_backing(base_qcow2.get(), qcow2->backing_qcow2.get()))
                FAILURE_1(IO, "%s: Base changed while mounting the overlay", _filename);
        }

        if (byte_size < (off_t_large)state.block_size)
        {
            printf("%s: Image %s is empty, too small, or its size could not be read.\n",
                   devid_string, _filename);
            restore_old();
            return false;
        }
        determine_layout();
    }
    catch (...)
    {
        restore_old();
        throw;
    }

    filename.swap(new_filename);
    discard_old();
    state.scsi.media_changed = -1;
    return true;
}

void CDiskFile::check_state()
{
    // Don't lock while the mailbox is empty, which is the normal state.
    if (!cdrom() || !media_mailbox || !media_mailbox->has_pending_actions())
        return;

    std::lock_guard<std::recursive_mutex> lock(media_action_mutex);
    if (scsi_get_phase(0) == SCSI_PHASE_FREE)
        service_pending_media_actions();
}

void CDiskFile::scsi_select_me(int bus)
{
    std::lock_guard<std::recursive_mutex> lock(media_action_mutex);
    CDisk::scsi_select_me(bus);
}

void CDiskFile::service_pending_media_actions()
{
    // avoid the mutex and penalties if nothing to do
    if (!media_mailbox || !media_mailbox->has_pending_actions())
        return;

    std::lock_guard<std::recursive_mutex> lock(media_action_mutex);

    std::deque<SDiskFileMediaAction> actions;
    if (!media_mailbox->take_pending_actions(actions))
        return;

    for (std::deque<SDiskFileMediaAction>::const_iterator it = actions.begin();
         it != actions.end(); ++it)
    {
        if (it->type == EDiskFileMediaAction::SetReadOnly)
        {
            const bool changed = set_read_only(it->read_only);
            media_mailbox->reconcile_read_only(read_only);
            printf("%s: Floppy is %s%s.\n", devid_string,
                read_only ? "read-only" : "writable",
                changed ? "" : " (unchanged: backing file could not be reopened)");
            continue;
        }

        if (it->type == EDiskFileMediaAction::CreateBlankFloppy)
        {
            bool created = false;
            bool mounted = false;
            try
            {
                created = create_blank_floppy(
                    it->path.c_str(), it->image_size);
                mounted = created && change_media(it->path.c_str());
            }
            catch (const std::exception& error)
            {
                printf("%s: Blank floppy creation failed: %s\n",
                    devid_string, error.what());
            }
            catch (...)
            {
                printf("%s: Blank floppy creation failed with an unknown error.\n",
                    devid_string);
            }

            if (mounted)
                media_mailbox->update_mounted_image(filename);

            if (mounted)
                printf("%s: Blank %zu KB floppy image created and mounted: %s\n",
                    devid_string, it->image_size / 1024, it->path.c_str());
            else if (created)
                printf("%s: Blank floppy image created but could not be mounted: %s\n",
                    devid_string, it->path.c_str());
            else
                printf("%s: Blank floppy image creation failed: %s\n",
                    devid_string, it->path.c_str());
            continue;
        }

        if (cdrom() && state.scsi.locked && !it->force_locked)
        {
            printf("%s: Media change refused because the guest locked the CD-ROM.\n",
                devid_string);
            continue;
        }
        if (cdrom() && state.scsi.locked && it->force_locked)
            printf("%s: Forcing operator-approved change of guest-locked CD-ROM.\n",
                devid_string);

        const bool ejecting = it->type == EDiskFileMediaAction::Eject;
        bool changed = false;
        try
        {
            changed = ejecting ? eject_media() :
                change_media(it->path.c_str());
        }
        catch (const std::exception& error)
        {
            printf("%s: Media change failed: %s\n",
                devid_string, error.what());
        }
        catch (...)
        {
            printf("%s: Media change failed with an unknown error.\n",
                devid_string);
        }
        if (changed && !ejecting)
            media_mailbox->update_mounted_image(filename);

        const char* kind = floppy_device ? "Floppy" : "CD-ROM";
        if (ejecting)
            printf("%s: %s media eject %s.\n", devid_string, kind,
                changed ? "complete" : "failed");
        else
            printf("%s: %s media change %s: %s\n", devid_string, kind,
                changed ? "complete" : "failed", it->path.c_str());
    }

    media_mailbox->reconcile_read_only(read_only);
}


// ===========================================================================
//  CDisk virtual interface overrides
// ===========================================================================

int CDiskFile::RestoreState(FILE* f)
{
    const int result = CDisk::RestoreState(f);
    if (result != 0)
        return result;

    // QCOW2 translates the restored logical position on the next I/O.
    if (qcow2)
        return 0;

    // seek_byte() and read_bytes() keep the saved position equal to the BIN/CUE 
    // logical position. Physical seeks happen on read.
    if (is_bincue)
    {
        if (state.byte_pos / 2048 > LONG_MAX)
        {
            printf("%s: Restored BIN/CUE position is not representable.\n",
                   devid_string);
            return -1;
        }
        logical_byte_pos = state.byte_pos;
        current_lba = (long)(state.byte_pos / 2048);
        return 0;
    }
    if (!handle)
    {
        if (!filename.empty() || byte_size != 0 || state.byte_pos != 0)
        {
            printf("%s: Restored image position has no backing file.\n",
                   devid_string);
            return -1;
        }
        logical_byte_pos = 0;
        current_lba = 0;
        return 0;
    }

    // Restore saved host cursor to correct position, otherwise bad things happen
    if (fseek_large(handle, state.byte_pos, SEEK_SET) != 0 ||
        ftell_large(handle) != state.byte_pos)
    {
        printf("%s: Could not restore image cursor for %s: %s\n",
               devid_string, filename.c_str(), strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * \brief Seek to an absolute logical byte position.
 *
 * For BIN/CUE images the position is logical (LBA * 2048); the physical
 * file seek is deferred to read_bytes() so that we can translate across
 * track boundaries without seeking unnecessarily.
 *
 * For plain images the behaviour is identical to the original code.
 **/
bool CDiskFile::seek_byte(off_t_large byte)
{
    if (!is_ready())
        return false;

    if (qcow2)
    {
        if (byte < 0 || byte > byte_size)
            FAILURE_1(InvalidArgument, "%s: Seek is outside the image", devid_string);
        state.byte_pos = byte;
        return true;
    }

    if (is_bincue)
    {
        if (byte >= byte_size)
        {
            FAILURE_1(InvalidArgument,
                      "%s: BIN/CUE seek beyond end of image!\n", devid_string);
        }

        logical_byte_pos = byte;
        current_lba      = (long)(byte / 2048);
        state.byte_pos   = byte;
        return true;
    }

    if (!handle)
        return false;

    // --- original plain-file path (unchanged) ---
    if (byte >= byte_size)
    {
        FAILURE_1(InvalidArgument,
                  "%s: Seek beyond end of file!\n", devid_string);
    }

    if (fseek_large(handle, byte, SEEK_SET) != 0)
        return false;
    state.byte_pos = ftell_large(handle);
    return state.byte_pos == byte;
}

/**
 * \brief Read \a bytes bytes from the current position into \a dest.
 *
 * For BIN/CUE images: translates logical byte/LBA positions to physical
 * file offsets, extracting only user-data bytes and skipping raw sector
 * headers. Correctly crosses track and file boundaries.
 *
 * For audio tracks the full raw sector (2352 bytes) is treated as payload
 * because there is no header to skip.
 *
 * For plain images the behaviour is identical to the original code.
 **/
size_t CDiskFile::read_bytes(void* dest, size_t bytes)
{
    if (!is_ready())
        return 0;

    if (is_bincue)
    {
        size_t total_read = 0;
        u8*    out        = static_cast<u8*>(dest);

        while (bytes > 0)
        {
            int         track_idx = 0;
            off_t_large file_off  = 0;

            if (!lba_to_file_position(current_lba, track_idx, file_off))
            {
                // LBA is beyond all tracks: pad with zeros and stop.
                printf("%s: BIN/CUE read past end of image at LBA %ld\n",
                       devid_string, current_lba);
                break;
            }

            const CueTrack* trk = &tracks[track_idx];

            // Byte offset within the current logical 2048-byte sector.
            long   offset_in_sector = (long)(logical_byte_pos % 2048);

            // How many logical bytes remain in this sector.
            size_t can_read = (size_t)(2048 - offset_in_sector);
            if (can_read > bytes)
                can_read = bytes;

            // Physical position: start of raw sector + header + intra-sector offset.
            off_t_large phys = file_off
                             + (off_t_large)trk->dataOffset
                             + (off_t_large)offset_in_sector;

            if (fseek_large(trk->fileHandle, phys, SEEK_SET) != 0)
            {
                printf("%s: BIN/CUE fseek failed for track %d\n",
                       devid_string, trk->number);
                break;
            }

            size_t r = fread(out, 1, can_read, trk->fileHandle);
            if (r == 0)
            {
                printf("%s: BIN/CUE fread returned 0 for track %d\n",
                       devid_string, trk->number);
                break;
            }

            out              += r;
            total_read       += r;
            bytes            -= r;
            logical_byte_pos += (off_t_large)r;
            state.byte_pos    = logical_byte_pos;
            current_lba       = (long)(logical_byte_pos / 2048);
        }

        return total_read;
    }

    if (!handle)
        return 0;

    if (qcow2)
    {
        qcow2_read(handle, qcow2.get(), static_cast<u64>(state.byte_pos), dest, bytes);
        state.byte_pos += static_cast<off_t_large>(bytes);
        return bytes;
    }

    // --- original plain-file path (unchanged) ---
    size_t r = fread(dest, 1, bytes, handle);
    state.byte_pos = ftell_large(handle);
    return r;
}

/**
 * \brief Write \a bytes bytes from \a src at the current position.
 *
 * BIN/CUE images are always read-only (write returns 0).
 * Plain image behaviour is identical to the original code.
 **/
size_t CDiskFile::write_bytes(void* src, size_t bytes)
{
    if (!is_ready())
        return 0;

    if (is_bincue)
    {
        // BIN/CUE images are treated as read-only media.
        return 0;
    }

    if (read_only)
        return 0;

    if (!handle)
        return 0;

    if (qcow2)
    {
        qcow2_write(static_cast<u64>(state.byte_pos), src, bytes);
        state.byte_pos += static_cast<off_t_large>(bytes);
        return bytes;
    }

    size_t r = fwrite(src, 1, bytes, handle);
    state.byte_pos = ftell_large(handle);
    return r;
}

std::string CDiskFile::file_identity(FILE* image) const
{
    std::ostringstream out;
#if defined(_WIN32)
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle((HANDLE)_get_osfhandle(_fileno(image)), &info))
        FAILURE_2(IO, "%s: Cannot identify image (error %lu)",
                  devid_string, GetLastError());
    out << info.dwVolumeSerialNumber << ':'
        << info.nFileIndexHigh << ':' << info.nFileIndexLow;
#else
    struct stat info;
    if (fstat(fileno(image), &info) != 0)
        FAILURE_2(IO, "%s: Cannot identify image: %s", devid_string, strerror(errno));
    out << info.st_dev << ':' << info.st_ino;
#endif
    return out.str();
}

size_t CDiskFile::read_file(FILE* image, u64 offset, void* data,
                           size_t count, bool exact)
{
    if (offset > u64((std::numeric_limits<off_t_large>::max)()))
        FAILURE_1(IO, "%s: File offset is too large for this host", devid_string);

    clearerr(image);

    if (fseek_large(image, static_cast<off_t_large>(offset), SEEK_SET) != 0)
        FAILURE_2(IO, "%s: Cannot seek image: %s", devid_string, strerror(errno));

    const size_t stored = fread(data, 1, count, image);

    if (ferror(image) || (stored != count && (exact || !feof(image))))
        FAILURE_2(IO, "%s: Cannot read image: %s", devid_string,
            ferror(image) ? strerror(errno) : "unexpected end of file");

    return stored;
}

void CDiskFile::write_file(FILE* image, u64 offset, const void* data,
                          size_t count)
{
    if (offset > u64((std::numeric_limits<off_t_large>::max)()))
        FAILURE_1(IO, "%s: File offset is too large for this host", devid_string);

    if (fseek_large(image, static_cast<off_t_large>(offset), SEEK_SET) != 0)
        FAILURE_2(IO, "%s: Cannot seek image: %s", devid_string, strerror(errno));

    const size_t stored = fwrite(data, 1, count, image);

    if (stored != count || ferror(image))
        FAILURE_2(IO, "%s: Cannot write image: %s", devid_string, strerror(errno));
}

/**
 * \brief Flush write buffers.
 *
 * BIN/CUE images are read-only so there is nothing to flush.
 * Synchronizing the backing store is reserved for snapshot preparation,
 * media changes and shutdown; guest flush requests retain their prior cost.
 **/
void CDiskFile::flush()
{
    if (qcow2)
    {
        if (qcow2->failed)
            FAILURE_1(IO, "%s: QCOW2: image is unavailable after an I/O failure; "
                "close and inspect it", devid_string);

        if (!read_only)
        {
            try
            {
                qcow2_set_dirty(false);
            }
            catch (...)
            {
                qcow2->failed = true;
                throw;
            }
        }

        return;
    }
    if (is_bincue)
        return;

    if (handle && !read_only && (fflush(handle) != 0 || ferror(handle)))
        FAILURE_2(Runtime, "Could not flush image %s: %s",
                  filename.c_str(), strerror(errno));
}

void CDiskFile::prepare_snapshot()
{
    if (qcow2)
        flush();
    else if (handle && !read_only && !is_bincue)
        sync_image();
}

void CDiskFile::sync_image()
{
    if (fflush(handle) != 0 || ferror(handle))
        FAILURE_2(IO, "Could not flush image %s: %s",
                  filename.c_str(), strerror(errno));
#if defined(_WIN32)
    const int result = _commit(_fileno(handle));
#else
    const int result = fsync(fileno(handle));
#endif
    if (result != 0)
        FAILURE_2(IO, "Could not synchronize image %s: %s",
                  filename.c_str(), strerror(errno));
}

bool CDiskFile::flush_before_media_change()
{
    try
    {
        prepare_snapshot();
        return true;
    }
    catch (const CException& error)
    {
        printf("%s: Media remains mounted: %s\n",
               devid_string, error.message().c_str());
        return false;
    }
    catch (const std::exception& error)
    {
        printf("%s: Media remains mounted: %s\n", devid_string, error.what());
        return false;
    }
}

FILE* CDiskFile::open_image(const char* path, const char* mode) const
{
    const bool shared = mode[0] == 'r' && !strchr(mode, '+');
#if defined(_WIN32)
    return _fsopen(path, mode, shared ? _SH_DENYWR : _SH_DENYRW);
#else
    FILE* image = fopen_large(path, mode);
    if (image && flock(fileno(image), (shared ? LOCK_SH : LOCK_EX) | LOCK_NB) != 0)
    {
        const int error = errno;
        fclose(image);
        errno = error;
        return nullptr;
    }
    return image;
#endif
}

bool CDiskFile::close_image(FILE*& image, const char* path) const noexcept
{
    if (!image)
        return true;
    FILE* closing = image;
    image = nullptr; // fclose consumes the stream even when it reports failure.
    if (fclose(closing) == 0)
        return true;
    const int error = errno;
    printf("%s: Could not close image %s: %s\n",
           devid_string, path, strerror(error));
    return false;
}

std::string CDiskFile::snapshot_identity() const
{
    std::ostringstream out;
    append_identity_string(out, CDisk::snapshot_identity());
    out << "disk-file-v1:" << read_only << ':' << is_cdrom << ':'
        << floppy_device << ':' << byte_size << ':';
    if (!handle && !is_bincue && filename.empty())
    {
        out << "empty";
        return out.str();
    }
    if (!is_bincue)
    {
        out << (qcow2 ? "qcow2:" : "raw:");
        append_identity_string(out, image_identity(handle, filename));
        for (const Qcow2State* base = qcow2.get(); base && base->backing;
             base = base->backing_qcow2.get())
        {
            out << "backing:";
            append_identity_string(out,
                image_identity(base->backing.get(), base->backing_filename));
        }
        return out.str();
    }

    out << "cue:" << track_count << ':';
    // The CUE itself is closed after parsing. But, we need to show guest the correct
    // layout, even if it changed somehow....
    std::unique_ptr<FILE, int (*)(FILE*)> cue(
        fopen(filename.c_str(), "rb"), &fclose);
    if (!cue)
        FAILURE_1(Runtime, "Cannot identify mounted CUE: %s", filename.c_str());
    append_identity_string(out, image_identity(cue.get(), filename));
    for (int i = 0; i < track_count; ++i)
    {
        const CueTrack& track = tracks[i];
        out << track.number << ':' << track.mode << ':' << track.sectorSize
            << ':' << track.dataOffset << ':' << track.dataSize << ':'
            << track.startLBA << ':' << track.endLBA << ':' << track.pregapLBA
            << ':' << track.fileOffset << ':';
        append_identity_string(out,
            image_identity(track.fileHandle, track.filename));
    }
    FILE* closing = cue.release();
    if (!close_image(closing, filename.c_str()))
        FAILURE(Runtime, "Could not close CUE identity handle");
    return out.str();
}


// ===========================================================================
//  BIN/CUE query helpers (public)
// ===========================================================================

/**
 * \brief Return the track that covers \a lba, or nullptr.
 **/
const CueTrack* CDiskFile::get_track_for_lba(long lba) const
{
    if (!is_bincue || !tracks)
        return nullptr;

    for (int i = 0; i < track_count; i++)
    {
        if (lba >= tracks[i].startLBA && lba < tracks[i].endLBA)
            return &tracks[i];
    }

    return nullptr;
}


// ===========================================================================
//  BIN/CUE private helpers
// ===========================================================================

/**
 * \brief Return the platform path separator character.
 *
 * Used when resolving .bin paths relative to the .cue file's directory.
 **/
char CDiskFile::path_separator()
{
#if defined(_WIN32)
    return '\\';
#elif defined(__VMS)
    return ']';
#else
    return '/';
#endif
}

/**
 * \brief Convert MSF address to LBA.
 **/
long CDiskFile::msf_to_lba(int m, int s, int f)
{
    return (long)(m * 60 + s) * 75 + f;
}

/**
 * \brief Convert LBA to MSF address.
 **/
void CDiskFile::lba_to_msf(long lba, int& m, int& s, int& f)
{
    f    = (int)(lba % 75); lba /= 75;
    s    = (int)(lba % 60); lba /= 60;
    m    = (int)lba;
}

/**
 * \brief Release all bin/cue state, leaving the object ready to load
 *        a fresh image (either bin/cue or plain).
 **/
void CDiskFile::reset_bincue_state()
{
    if (tracks)
    {
        close_bin_handles();
        free(tracks);
        tracks = nullptr;
    }

    is_bincue        = false;
    track_count      = 0;
    current_lba      = 0;
    logical_byte_pos = 0;
}

/**
 * \brief Parse a .cue sheet and populate the tracks[] array.
 *
 * Returns true on success (tracks[] is allocated and populated,
 * all .bin files are open).  Returns false on any error; on failure
 * the tracks[] array is freed and the object is left in a clean state.
 *
 * Design notes
 * ------------
 * - Two-pass approach: first pass counts TRACK keywords so we can
 *   allocate exactly the right amount of memory with a single calloc,
 *   avoiding any std::vector dependency.
 * - Fixed-size char arrays throughout: no heap allocations inside the
 *   track structures, no C++ exceptions.
 * - Robust against DOS/Unix/Mac line endings (\r\n, \n, \r).
 * - Path joining uses path_separator() for cross-platform correctness.
 * - OpenVMS note: VMS path syntax is  DEVICE:[DIR]FILE.EXT  The last
 *   ']' is the directory-close character used as path_separator() on VMS,
 *   which means get_cue_directory() correctly strips to the directory
 *   portion (DEVICE:[DIR]) that can be prefixed to relative .bin names.
 **/
bool CDiskFile::try_parse_cue(const char* cue_path)
{
    // ------------------------------------------------------------------
    // Open the .cue file
    // ------------------------------------------------------------------
    FILE* f = fopen(cue_path, "r");
    if (!f)
    {
        printf("%s: Cannot open .cue file: %s\n", devid_string, cue_path);
        return false;
    }

    // ------------------------------------------------------------------
    // Resolve the directory that contains the .cue file.
    // .bin filenames in the cue sheet are interpreted relative to this.
    // ------------------------------------------------------------------
    char cue_dir[BINCUE_MAX_PATH] = "";
    {
        const char  sep      = path_separator();
        const char* last_sep = nullptr;
        const char* p        = cue_path;
        while (*p)
        {
            if (*p == sep) last_sep = p;
            p++;
        }

        if (last_sep)
        {
            size_t dir_len = (size_t)(last_sep - cue_path);
            if (dir_len >= sizeof(cue_dir))
                dir_len = sizeof(cue_dir) - 1;
            memcpy(cue_dir, cue_path, dir_len);
            cue_dir[dir_len] = '\0';
        }
        // If last_sep is nullptr the .cue is in the CWD; cue_dir stays "".
    }

    // ------------------------------------------------------------------
    // First pass: count TRACK keywords to size the allocation.
    // ------------------------------------------------------------------
    char line[512];
    int  num_tracks = 0;

    while (fgets(line, (int)sizeof(line), f))
    {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "TRACK ", 6) == 0)
            num_tracks++;
    }

    if (num_tracks == 0)
    {
        printf("%s: .cue contains no TRACK entries.\n", devid_string);
        fclose(f);
        return false;
    }

    // ------------------------------------------------------------------
    // Allocate track array (zero-initialised).
    // ------------------------------------------------------------------
    tracks = static_cast<CueTrack*>(calloc((size_t)num_tracks, sizeof(CueTrack)));
    if (!tracks)
    {
        printf("%s: Out of memory allocating %d track descriptors.\n",
               devid_string, num_tracks);
        fclose(f);
        return false;
    }

    // ------------------------------------------------------------------
    // Second pass: populate track array.
    // ------------------------------------------------------------------
    rewind(f);

    int  current_idx  = -1;   // index into tracks[] for current TRACK block
    char current_bin[BINCUE_MAX_PATH] = ""; // most-recently-seen FILE path

    while (fgets(line, (int)sizeof(line), f))
    {
        // Strip leading whitespace.
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;

        // Strip trailing whitespace / line endings.
        char* end = p + strlen(p);
        while (end > p &&
               (*(end-1) == '\r' || *(end-1) == '\n' || *(end-1) == ' '))
            --end;
        *end = '\0';

        if (*p == '\0' || *p == ';' || *p == '#')
            continue; // blank or comment

        // ---- FILE "filename.bin" BINARY ---------------------------
        if (strncmp(p, "FILE ", 5) == 0)
        {
            char* q1 = strchr(p, '"');
            char* q2 = q1 ? strchr(q1 + 1, '"') : nullptr;

            if (!q1 || !q2)
            {
                // Some tools omit quotes; grab token after "FILE ".
                char raw[BINCUE_MAX_PATH] = "";
                sscanf(p + 5, "%511s", raw);
                q1 = raw - 1;   // reuse pointer trick won't work here
                // Simpler: just copy raw directly.
                strncpy(current_bin, raw, BINCUE_MAX_PATH - 1);
                current_bin[BINCUE_MAX_PATH - 1] = '\0';
            }
            else
            {
                size_t name_len = (size_t)(q2 - q1 - 1);
                if (name_len >= BINCUE_MAX_PATH)
                    name_len = BINCUE_MAX_PATH - 1;
                memcpy(current_bin, q1 + 1, name_len);
                current_bin[name_len] = '\0';
            }

            // Resolve to absolute path when cue_dir is known and the
            // filename is not already absolute.
            //
            // Absolute detection:
            //   Windows  – starts with '\' or "X:"
            //   VMS      – starts with a letter followed by ':' (device)
            //   Unix     – starts with '/'
            bool is_absolute = false;
#if defined(_WIN32)
            is_absolute = (current_bin[0] == '\\' ||
                           (current_bin[0] != '\0' && current_bin[1] == ':'));
#elif defined(__VMS)
            is_absolute = (current_bin[0] != '\0' && current_bin[1] == ':');
#else
            is_absolute = (current_bin[0] == '/');
#endif
            if (!is_absolute && cue_dir[0] != '\0')
            {
                char tmp[BINCUE_MAX_PATH];
                snprintf(tmp, sizeof(tmp), "%s%c%s",
                         cue_dir, path_separator(), current_bin);
                strncpy(current_bin, tmp, BINCUE_MAX_PATH - 1);
                current_bin[BINCUE_MAX_PATH - 1] = '\0';
            }
        }

        // ---- TRACK nn MODE ----------------------------------------
        else if (strncmp(p, "TRACK ", 6) == 0)
        {
            current_idx++;
            if (current_idx >= num_tracks)
            {
                // Shouldn't happen; guard against corrupted cue files.
                printf("%s: More TRACKs found than counted – stopping parse.\n",
                       devid_string);
                break;
            }

            CueTrack* trk = &tracks[current_idx];

            char mode_str[32] = "";
            int  track_num    = 0;
            sscanf(p + 6, "%d %31s", &track_num, mode_str);

            trk->number = track_num;
            trk->mode   = parse_mode_string(mode_str,
                                             trk->sectorSize,
                                             trk->dataOffset,
                                             trk->dataSize);

            strncpy(trk->filename, current_bin, BINCUE_MAX_PATH - 1);
            trk->filename[BINCUE_MAX_PATH - 1] = '\0';

            // startLBA / endLBA filled in by INDEX lines below.
            trk->startLBA   = 0;
            trk->endLBA     = 0;
            trk->pregapLBA  = 0;
            trk->fileOffset = 0;
            trk->fileHandle = nullptr;

            printf("%s:   Track %02d  %s  sectorSize=%d  dataOffset=%d\n",
                   devid_string, track_num, mode_str,
                   trk->sectorSize, trk->dataOffset);
        }

        // ---- INDEX nn mm:ss:ff ------------------------------------
        else if (strncmp(p, "INDEX ", 6) == 0)
        {
            if (current_idx < 0) continue;

            int index_num = 0, m = 0, s = 0, fr = 0;
            if (sscanf(p + 6, "%d %d:%d:%d", &index_num, &m, &s, &fr) == 4)
            {
                long lba = msf_to_lba(m, s, fr);
                if (index_num == 0)
                    tracks[current_idx].pregapLBA = lba;
                else if (index_num == 1)
                    tracks[current_idx].startLBA  = lba;
                // Higher index values (index points) are ignored.
            }
        }

        // ---- PREGAP mm:ss:ff --------------------------------------
        else if (strncmp(p, "PREGAP ", 7) == 0)
        {
            if (current_idx < 0) continue;

            int m = 0, s = 0, fr = 0;
            if (sscanf(p + 7, "%d:%d:%d", &m, &s, &fr) == 3)
                tracks[current_idx].pregapLBA = msf_to_lba(m, s, fr);
        }

        // ---- Other keywords (TITLE, PERFORMER, …) -----------------
        // Silently ignored; we only care about structural keywords.
    }

    fclose(f);

    track_count = current_idx + 1;

    if (track_count <= 0)
    {
        printf("%s: .cue parse produced no usable tracks.\n", devid_string);
        free(tracks);
        tracks = nullptr;
        track_count = 0;
        return false;
    }

    // ------------------------------------------------------------------
    // Compute endLBA for every track except the last.
    // The last track's endLBA is determined from the .bin file size after
    // we open the files below.
    // ------------------------------------------------------------------
    for (int i = 0; i < track_count - 1; i++)
        tracks[i].endLBA = tracks[i + 1].startLBA;

    // ------------------------------------------------------------------
    // Open all .bin files and finalise fileOffset / last endLBA.
    // ------------------------------------------------------------------
    if (!open_bin_files())
    {
        // open_bin_files() already printed an error and cleaned up.
        free(tracks);
        tracks      = nullptr;
        track_count = 0;
        return false;
    }

    return true;
}

/**
 * \brief Open the .bin file for every track and compute physical offsets.
 *
 * Returns false and closes any already-opened handles on failure.
 *
 * Note on multi-file CUEs: each track may reference a different .bin file.
 * In the common single-file case all tracks share the same path but we
 * still open one handle per track for simplicity; OS file caching means
 * this has no practical cost.
 *
 * fileOffset for each track is the byte offset within its .bin file at
 * which the track's raw sector data begins (startLBA * sectorSize for
 * single-bin images; 0 for per-track bin files).
 **/
bool CDiskFile::open_bin_files()
{
    // Detect whether this is a single-bin or multi-bin layout by checking
    // whether all tracks share the same filename.
    bool single_bin = true;
    for (int i = 1; i < track_count; i++)
    {
        if (strcmp(tracks[0].filename, tracks[i].filename) != 0)
        {
            single_bin = false;
            break;
        }
    }

    for (int i = 0; i < track_count; i++)
    {
        CueTrack* trk = &tracks[i];

        trk->fileHandle = fopen_large(trk->filename, "rb");
        if (!trk->fileHandle)
        {
            printf("%s: Cannot open BIN file: %s\n",
                   devid_string, trk->filename);
            close_bin_handles();
            return false;
        }

        if (single_bin)
        {
            // All tracks live in one .bin; each track starts at
            // startLBA * sectorSize bytes into the file.
            trk->fileOffset = (off_t_large)trk->startLBA * trk->sectorSize;
        }
        else
        {
            // Per-track .bin: track data starts at byte 0 of its file.
            trk->fileOffset = 0;
        }

        // For the last track, determine endLBA from the file size.
        if (i == track_count - 1 && trk->endLBA == 0)
        {
            fseek_large(trk->fileHandle, 0, SEEK_END);
            off_t_large bin_bytes = ftell_large(trk->fileHandle);

            long raw_sectors;
            if (single_bin)
            {
                // The file may contain data for all tracks; compute
                // how many sectors belong to the last track.
                off_t_large preceding_bytes =
                    (off_t_large)trk->startLBA * trk->sectorSize;
                off_t_large last_track_bytes = bin_bytes - preceding_bytes;
                raw_sectors = (long)(last_track_bytes / trk->sectorSize);
            }
            else
            {
                raw_sectors = (long)(bin_bytes / trk->sectorSize);
            }

            trk->endLBA = trk->startLBA + raw_sectors;
        }

        printf("%s:   Track %02d  LBA %ld..%ld  file=%s  offset=%" PRId64 "\n",
               devid_string, trk->number,
               trk->startLBA, trk->endLBA,
               trk->filename, trk->fileOffset);
    }

    return true;
}

/**
 * \brief Close all open .bin file handles without freeing the array.
 **/
void CDiskFile::close_bin_handles()
{
    if (!tracks) return;

    for (int i = 0; i < track_count; i++)
    {
        if (tracks[i].fileHandle)
        {
            close_image(tracks[i].fileHandle, tracks[i].filename);
        }
    }
}

/**
 * \brief Parse a cue track-mode string and populate sector geometry.
 *
 * Returns the matching enum value. Falls back to TRACK_MODE1_2352
 * for unrecognised strings and prints a warning.
 *
 * Sector geometry reference
 * -------------------------
 * AUDIO        : 2352 / offset 0  / data 2352  (raw PCM)
 * MODE1/2048   : 2048 / offset 0  / data 2048  (cooked)
 * MODE1/2352   : 2352 / offset 16 / data 2048
 * MODE2/2336   : 2336 / offset 8  / data 2048
 * MODE2/2352   : 2352 / offset 24 / data 2048
 **/
CueTrackMode CDiskFile::parse_mode_string(const char* s,
                                           int& sector_size,
                                           int& data_offset,
                                           int& data_size)
{
    if (strcmp(s, "AUDIO") == 0)
    {
        sector_size = 2352; data_offset = 0;  data_size = 2352;
        return TRACK_MODE_AUDIO;
    }
    if (strcmp(s, "MODE1/2048") == 0)
    {
        sector_size = 2048; data_offset = 0;  data_size = 2048;
        return TRACK_MODE1_2048;
    }
    if (strcmp(s, "MODE1/2352") == 0)
    {
        sector_size = 2352; data_offset = 16; data_size = 2048;
        return TRACK_MODE1_2352;
    }
    if (strcmp(s, "MODE2/2336") == 0)
    {
        sector_size = 2336; data_offset = 8;  data_size = 2048;
        return TRACK_MODE2_2336;
    }
    if (strcmp(s, "MODE2/2352") == 0)
    {
        sector_size = 2352; data_offset = 24; data_size = 2048;
        return TRACK_MODE2_2352;
    }

    // Unrecognised mode: warn and default to MODE1/2352.
    printf("CDiskFile: Unknown track mode '%s', defaulting to MODE1/2352\n", s);
    sector_size = 2352; data_offset = 16; data_size = 2048;
    return TRACK_MODE1_2352;
}

/**
 * \brief Translate a logical LBA into a physical track index and byte offset.
 *
 * \param lba         Logical block address to look up.
 * \param track_idx   Receives the index into tracks[].
 * \param file_offset Receives the byte offset in tracks[track_idx].fileHandle
 *                    at which the *raw* sector for \a lba begins (before the
 *                    dataOffset header skip).
 * \return true on success, false if lba is out of range.
 **/
bool CDiskFile::lba_to_file_position(long lba,
                                      int& track_idx,
                                      off_t_large& file_offset) const
{
    for (int i = 0; i < track_count; i++)
    {
        if (lba >= tracks[i].startLBA && lba < tracks[i].endLBA)
        {
            track_idx = i;
            long sector_in_track = lba - tracks[i].startLBA;
            file_offset = tracks[i].fileOffset
                        + (off_t_large)sector_in_track * tracks[i].sectorSize;
            return true;
        }
    }

    return false;
}
