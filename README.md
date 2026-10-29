# DEC ES40 Simulator

es40 is free software; see the License section below.

For building and installation instructions please see below.

For release sources, download `es40-<version>-source.zip` or
`es40-<version>-source.tar.gz` from the release assets. These packages include the
pinned third-party submodules; GitHub's automatic "Source code" downloads omit
them. When cloning instead, use `git clone --recurse-submodules`.

Windows 11 with VS2026 and X64 builds only is the main development environment.

Npcap provides bridged networking; the Rust SLIRP backend provides userspace NAT.
macOS VMnet support exists as well as TAP networking support for non-windows hosts.
Use "NN" builds if you do not need networking.

Latest VC redist may be required to run binaries, available here:  
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist  
Latest 2017-2026 x64 redistributable is recommended.  
At minimum the version matching your build toolchain is needed.

For outdated documentation, please see the files in the docs subdirectory.

### Please use es40-cfg to generate a config the first time you use it to ensure you do not omit any required devices from the chipset.
### Reference src\es40.cfg for configuration values and explanations  
  
## License  
  
ES40 as a whole is distributed under the GNU General Public License, version 2  
(see COPYING), because it contains GPL-2.0 code. Files derived from Bochs are  
under the GNU Lesser General Public License (see COPYING.LGPL).  

Each source file states its own license in its header.  

It is preferred that new ES40 code is BSD-1-Clause so it can be reused for 
emulation improvement everywhere, and that code from other projects keeps its 
original license instead of relicensing to more restrictive ones. 
  
The third-party notices that must accompany binaries are in src/es40-NOTICES.txt.  
  
Contribution licensing preferences are in CONTRIBUTING.md.  

# Live shot of X11 running on emulated S3 via SDL display!

![Screenshot](https://github.com/ES40-Emu/es40/blob/main/screenshots/OpenVMS.png?raw=true)

------------------------------------------------------------------------

# Building Instructions for Windows

We'll need both npcap and SDL-3.4 for full featured es40 builds.

If you build only NS target configurations, then you do not need SDL.  
If you build only NN target configurations, then you do not need npcap.  
If you build NS NN target configurations, then you do not need either.  
  
## SDL:

Retrieve SDL from https://github.com/libsdl-org/SDL

For simplicity, I had extracted and configured include and link directories
for all es40 targets/configurations to look for SDL under C:\dev\SDL\ - a simple
find and replace in the .vcxproj files can change this if you want - eg find
"C:\dev\SDL\" and replace with your desired SDL location.

Extract the root of the repository to C:\dev\SDL\

Under C:\dev\SDL3\VisualC\ there is a SDL.sln file - open this in VS2022/26.

Accept the 'trust and continue' dialog if it is displayed

If prompted to retarget the solution, select the desired Windows SDK and
toolchain version

Build the solution for x64 Debug and Release configurations.

## npcap:

If you wish to build only NN target configurations, you can skip this step.

If you wish to build all configurations, but not run, you only need to extract
the npcap SDK to C:\dev\npcap-sdk\

To run network enabled binaries, you will need to install npcap itself.  
The installer and SDK for npcap can be found here: https://npcap.com/#download

## es40:

After your pathing is fixed in the vcxproj files or you used the default
c:\dev\ locations and structure, open `Visual Studio\es40.sln` from the repository root.

Run Build solution for individual configurations, or batch build to build
all configurations.

SDL3.dll will be required to be placed with the compiled es40 application, for
debug x64 build it would be placed in this location: Visual Studio\x64\Debug
or wherever you copy es40.exe to.

SDL3.dll will be found in C:\dev\SDL3\VisualC\x64\Debug for example
if you built x64 debug release configuration.

Make sure to set the debug working directory in project settings as
`$(OutDir)` for this configuration, the command being `$(TargetPath)` as is set
by default is fine, however.

Resulting binaries will be under `Visual Studio\x64\Debug`, `Visual Studio\x64\Release`, etc,
with the binary names being similar to es40 Release NS NN.exe.

## Rust SLIRP networking:  
  
Visual Studio lists the Rust sources in the Network filters but does not build  
them. Build the x64 MSVC static library separately with Rust 1.89 or newer, then  
enable it for the solution and its configuration tools:  
  
```bat
cargo build --release --locked --manifest-path src/network/slirp-rs/Cargo.toml
msbuild "Visual Studio/es40.sln" /m /p:Configuration=Release /p:Platform=x64 /p:Es40EnableSlirp=true
```
  
The default library path is `src/network/slirp-rs/target/release/es40_slirp.lib`.  
Use `/p:Es40SlirpLibrary=path\to\es40_slirp.lib` to select another prebuilt library.  
SLIRP stays disabled in NN configurations. CI builds it for every networked  
configuration alongside the other supported features. OpenVMS always excludes  
SLIRP, including when `HAVE_SLIRP` is explicitly defined.  
  
------------------------------------------------------------------------

# Building Instructions for Linux

For distributions not including SDL3 development libraries, see the [SDL Wiki](https://wiki.libsdl.org/SDL3/README-linux) on how to build and install these.

libpcap comes included in many Linux distributions. If it isn't, install it e.g. for apt-based systems by
```sh
sudo apt install libpcap-dev
```

Configuring and building can both be done by `autoconf` and `cmake`.

To build using `autoconf`, do
```sh
./autogen.sh
./configure
make
```
See `./configure --help` for options.

To build using `cmake`, do
```
cmake -S . -B build [-Doption=ON ...]
cmake --build build
```
See `CMakeLists.txt` for options to define. To point `cmake` to the SDL3 installation, use `-DSDL3_DIR=path-to-sdl3`.

On Linux, ES40 prefers SDL's Zenity file picker for opening media images and
saving blank floppy images when `zenity --version` succeeds. Otherwise it keeps
SDL's normal picker selection; Zenity is optional. An explicit
`SDL_FILE_DIALOG_DRIVER` setting takes precedence. If the normal desktop portal
rejects a process with pcap file capabilities, that error can still occur when
Zenity is unavailable.

# Firmware and ROM images

The emulator needs the AlphaServer ES40 console firmware (SRM), which belongs to
HP and is not part of this project. Everything else it needs is either created
on first start (`flash.rom`, `dpr.rom`, `toy.rom`) or available as free software.

| File | What it is | Where to get it |
| --- | --- | --- |
| `cl67srmrom.exe` | SRM firmware update image for the ES40 (`rom.srm`) | HP Alpha Systems Firmware Update CD V7.3, file `CL67SRMROM.EXE`. Only needed until the flash is programmed. |
| `flash.rom` | The machine's 2 MB flash (`rom.flash`); once programmed it holds SRM | Programmed by the emulator from `cl67srmrom.exe` at first start, or ready-made: <https://archive.org/details/es40-flasher> (`flash-programmed.zip`, SRM V7.3-1) |
| `vgabios.bin` | Video BIOS for the S3 Trio64 (`rom` in the `s3` section) | Free: GNU vgabios, Debian/Ubuntu package `vgabios` (`/usr/share/vgabios/vgabios.bin`, set `wake_on_reset = true`). Or a dump of a real S3 Trio64 BIOS such as `86c764x1.bin`. |
| GLoria, Radeon, PowerStorm, LSI ROMs | Option ROMs of the experimental cards | Dumps of the real cards or the vendor firmware package; not needed for a normal machine. |

The Linux configuration editor (`es40_cfg_gtk`) has an **Import firmware…**
button on its System page: give it `cl67srmrom.exe`, a programmed `flash.rom`,
or the `.zip`/`.iso` that contains one, and it places the file in the `rom`
folder beside the machine and points the configuration at it (`.iso` and `.zip`
reading uses `7z`, `unzip` or `isoinfo`, whichever is installed). The S3 card's
page offers the installed GNU vgabios with one click.

------------------------------------------------------------------------  
  
## Status  

## 9/30/2026 - Digital PowerStorm 300/350 (experimental)  
Experimental PowerStorm 300/350 (REALimage 2100) is available as  
the `powerstorm` device. Almost certianly completely broken, but with the  
300 VGA BIOS (I haven't found a 350 image yet) achieves SRM.    

## 9/29/2026 - ATI Radeon 7500 (experimental)  
Experimental ATI Radeon 7500 (RV200) graphics are available as the `radeon`  
device: standard VGA plus native 2D by PIO. No 3D, command processor or DMA.  
Firmware POST and guest drivers are untested.  
  
## 9/27/2026 - ELSA GLoria Synergy (experimental)  
Experimental ELSA GLoria Synergy (Permedia 2) graphics are available as the  
`gloria` device. Unfinished, many parts don't work/exist.  
  
## 9/15/26 - Saved-state compatibility  
  
Saved-state format 2.3 includes VGA/S3 registers, video memory, the graphics  
accelerator, adds device/media checks, audio state, RAM-disk contents,  
and flash command state. Older state files are rejected. Failed saves preserve  
the previous state file.  
  
Use the same emulator build, memory/device configuration, and unchanged mounted  
disk images. File-backed image contents are not included. Physical disk devices  
do not yet support snapshots.  
  
## 7/30/26 - JIT large pages
On Windows, there is a JIT large page functionality that requires the
SeLockMemoryPrivilege in windows. You may have to enable this if you do not see
[JIT][CPU0] block cache 38 MB (large pages), trace cache 4 MB (large pages)
on startup.  
On Windows Professional and higher editions, this is found in
secpol.msc under Local Policies -> User Rights Assignment -> Lock pages in memory
Linux does not require privilege for this. This provides a minor performance
boost to JIT.

## 6/13/26 - JIT support is here!
Working for Windows 2210, OpenVMS, and Tru64. No other OSes tested.  
Non-windows JIT support is experimental but should be fine.  
OpenVMS is reading approximately 80 VUPS on my machine compared to interp
30 VUPS. So in that benchmark, about 2.5x improvement.  

## 5/8/26 - TOY clock issue - SOUND SUPPORT! ES1370 Audio device!
For VMS it was not a one time boot prompt, I was wrong about how VMS handles
the time offsets. It sets the clock WITHOUT ARC's 1980 assumption.  
So, we now make it a configurable flag.  

The ES1370 audio device outputs via the SDL layer. You can use guest OS volume
controls or OS mixer level controls instead.

DECterm in OpenVMS causing machine check failures is FIXED!  
This was the result of numerous CPU accuracy fixes.

If you build from source now, also, you can disable the custom emulator PALcode
implementations for OpenVMS as well, and run with purely native PALcode instead
of the accelerated implementations for debugging purposes and experimentation.

Interestingly enough, only OpenVMS PALcode was visibly hurt by the flaws that
fixed it, OSF and NT PALcode weren't visibly affected (No OS flaws seen), but
VMS had ACCVIO 0in SECURESHRP on boot when using native PALcode, essentially
breaking OpenVMS entirely.

Networking in Tru64 appears to work, at least winth manual configuration!

## 5/7/26 - This one deserves its own notice. TOY clock handling change.
ARC base is 1980. This would make year byte 26 in TOY byte 9 2006 instead of
2026. But for everyone with installed VMS disk images out there, your saved
clock data in VMS will see a jump from 26 to 46 (which makes ARC correct).

This, of course, means in 2079 we will have to binary patch ARC to move the
base to allow for dates beyond 2079 to be displayed. Unkown how this would
affect Windows NT variants.

## 5/7/26 - Tru64 support. Red Hat 7.2 works. Timers fixed.
Thanks to a surprising AXPbox we have finalized the remaining SCSI flaws
that were blocking Tru64, and as well implemented some user-friendly features.
Red Hat 7.2's X11 doesn't play nicely yet though....  
OS Support list looks like this now:

Windows NT4 with graphics  
Windows 2K RCs and AXP64 2210 with some effort with graphics  
OpenVMS 8.4-2L1 and 2L2 with graphics  
Tru64 5.1B with graphics  
SOME Linux - Red Hat Enigma / 7.2 tested X11 flawed, needs to be fixed  
NetBSD 6 through 10.1 at minimum  
OpenBSD 4.8, 7.7, and 7.8  

Networking is only confirmed working on OpenVMS, however it may work on Tru64.

For windows hosts, we now have an emulated MPU-401 MIDI output device. Not full
sound, but MIDI players can send out music from the emulator now.

## 4/30/26 - Windows NT support
That was a wild ride.

## 4/24/26 - ARC works!
Lingering screen corruption issue - memtest failure at 0x80000  

Flash system from v73.iso with all options - exit the first stage updater
and say yes to manual update, update all flash options - RMC will fail
this is expected. ARC will now load/run from flash.  

Screen blanking before memtest failure and screen corruption expected
ARC large sweep clobbers several areas of memory. HW correctness fix to
follow shortly to correct this.  

## 4/11/26 - BEHAVIOR CHANGE IN SERIAL PORT OPERATION
### I removed LF stripping and modified telnet negotiation.
It now negotiates telnet binary mode for telnet clients
For non-telnet clients no negotiation happens, so any socket connection to
the serial port remains without telnet connection just passes raw data.

This is a big change in how the serial port operates. It now only drips in data
as fast as the OS can read it, avoiding overloading/crashes/overflows, etc.
I was getting TPU crashes before when pasting large amounts of data, among
other issues. Those are all now resolved.

## 3/1/26 - NetBSD fixes, SCSI fixes, etc
Note: For BSDs, you need to 'set boot_osflags a' in SRM for them to be able to
boot successfully.

## 2/28/26 - S3 text rendering resolved. Keyboard input buffer resolved. Tru64
OpenVMS X11 display is almost flawless!  
Mouse support has been added and works great!  
Tru64 X11 now displays from the install CD of 5.1B, however install sill
fails on SCSI disk. To be resolved. Keyboard issues have been resolved, no more
buffer full while using VGA console.

## 2/20/26 - S3 Update, Cirrus exlcuded. SDL3 conversion
X11 has basic display support under OpenVMS now, some text corruption issues
exist that are being worked on. Mouse input support is currently non-present.
Boot to login and CDE desktop works.  

Cirrus is being excluded from builds now, as it is just a generic VGA card
implementation and after render path reworks will need some tending to for
conversion.  

Upgraded to SDL3. Brings lots of new goodies. Gets rid of archaic SDL1.2.  
Incrimented version to 0.5 due to major changes across the board such as above.

## 2/14/26 - S3 Graphics port from MAME, work towards ARC support
Working through the S3 implementation, a more feature-complete and functional
implementation is found in MAME, though we already implemented some functions
that MAME does not yet. So we've refactored a lot of the code which, while not
yet complete, has already brought along some interesting improvements, such
as better text-mode graphics and somewhat more concise code. Contribution back
to MAME upstream for this code will be forthcoming for the items we implemented
prior that MAME does not have.  

Large comments and functions that documented VGA behavior and standards
information have been moved to 'old_vga_docs.txt' to clean up so that our code
more closely tracks MAME's S3 implementation to better enable simple diff'ing
and code sharing as progress is made.  

Some basic work has been done in general as well in the direction of getting
ARC firmware working.

## 12/24/25 - Build housekeeping
All configurations now build cleanly. Default location for include and lib
files is now c:\dev\SDL3\ and c:\dev\npcap-sdk\ - you can change this by doing
a simple find/replace in the .vcxproj files if you want to use different paths.
Simple POKEQ/POKEL support added to IDB as well.  
NN = No Network  
NS = No Screen (VGA Console)  
LSM/LSS = Lockstep Master/Slave builds.  
IDB = Integrated Debugger build.  

## 12/19/25 - Somewhat working full FLASH ROM support.
After doing the initial bootstrap with cl67srmrom.exe, you can now use the
HP firmware update CD to flash the ES40 firmware, including ARC/Abios, which
doesn't work yet, but you can as of this point at least downgrade via the
HP firmware update CD to SRM V7.2-1, which does work, and then boot off of
the flash rom directly on that version.

## 8/27/25 Actual S3 VBIOS WORKS!
S3 Incomplete, but it boots and executes SRM!  
Use S3Trio64 bios 86c764x1.bin

------------------------------------------------------------------------
