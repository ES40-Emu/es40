// license:BSD-3-Clause
// copyright-holders:Barry Rodewald

#include "emu.h"
#include "ibm8514a.h"   
#include "VGA.h" // es40 req

//#define VERBOSE (LOG_GENERAL)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

enum
{
	IBM8514_IDLE = 0,
	IBM8514_DRAWING_RECT,
	IBM8514_DRAWING_LINE,
	IBM8514_DRAWING_BITBLT,
	IBM8514_DRAWING_PATTERN,
	IBM8514_DRAWING_SSV_1,
	IBM8514_DRAWING_SSV_2,
	MACH8_DRAWING_SCAN
};

#define IBM8514_LINE_LENGTH (m_vga->offset())

DEFINE_DEVICE_TYPE(IBM8514A, ibm8514a_device, "ibm8514a", "IBM 8514/A Video")


ibm8514a_device::ibm8514a_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock)
	: ibm8514a_device(mconfig, IBM8514A, tag, owner, clock)
{
}

ibm8514a_device::ibm8514a_device(const machine_config& mconfig, device_type type, const char* tag, device_t* owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock)
	, m_vga(*this, finder_base::DUMMY_TAG)
{
}

// ES40 
ibm8514a_device::ibm8514a_device()
	: device_t()
	, m_vga()
{
}
// ES40

void ibm8514a_device::device_start()
{
	memset(&ibm8514, 0, sizeof(ibm8514));
	ibm8514.color_cmp = 0;
	ibm8514.color_cmp_enabled = false;
	ibm8514.color_cmp_src_ne = false;
	ibm8514.frgd_sel = 1;       // default: foreground color register
	ibm8514.bkgd_sel = 0;       // default: background color register
	ibm8514.frgd_mix_mode = 7;  // default: NEW (src)
	ibm8514.bkgd_mix_mode = 7;  // default: NEW (src)
	ibm8514.force_busy = false;
	ibm8514.force_busy2 = false;
	ibm8514.cmd_back = 0;
	ibm8514.fifo_idx = 0;
	ibm8514.dst_base = 0;
	ibm8514.src_base = 0;
	ibm8514.multifunc_misc2 = 0;
}

//es40
static inline uint32_t pixtrans_set_endian(uint32_t pixel_xfer, uint8_t bus_size, uint8_t color_bpp, uint16_t current_cmd)
{
	uint32_t xfer;
	int data_size = 8;
	if (bus_size == 0)  // 8-bit
		data_size = 8;
	if (bus_size == 1)  // 16-bit
		data_size = 16;
	if (bus_size >= 2)  // 32-bit
		data_size = 32;
	xfer = pixel_xfer;  // swap for 16-bit bus size in 16bpp only
	if ((current_cmd & 0x1000) && (data_size == 16) && (color_bpp == 1)) {
		xfer = ((xfer & 0x00ff) << 8) | ((xfer & 0xff00) >> 8);
	}
	return xfer;
}

static inline uint32_t pixtrans_lane_u32(uint32_t pixel_xfer, uint8_t bus_size, uint8_t color_bpp, uint32_t lane)
{
	// bus_size: 0=8-bit, 1=16-bit, >=2=32-bit
	int bs = (bus_size >= 2) ? 2 : bus_size;
	const uint32_t lanes = 1u << bs;           // 1, 2, 4
	lane *= (color_bpp + 1);
	lane &= (lanes - 1);

	return (pixel_xfer >> (lane * 8));
}

uint32_t ibm8514a_device::ibm8514_mix(uint8_t mix_mode, uint32_t src, uint32_t dst)
{
	switch (mix_mode & 0x0f)
	{
	case 0x00: return ~dst;
	case 0x01: return 0;
	case 0x02: return ~0;
	case 0x03: return dst;
	case 0x04: return ~src;
	case 0x05: return src ^ dst;
	case 0x06: return ~(src ^ dst);
	case 0x07: return src;
	case 0x08: return ~(src & dst);
	case 0x09: return (~src) | dst;
	case 0x0a: return src | (~dst);
	case 0x0b: return src | dst;
	case 0x0c: return src & dst;
	case 0x0d: return src & (~dst);
	case 0x0e: return (~src) & dst;
	case 0x0f: return ~(src | dst);
	default:   return src;            // shouldn't reach here
	}
}

void ibm8514a_device::ibm8514_do_pixel(uint32_t dest_offset, uint32_t src_offset, bool use_fgmix)
{
	dest_offset %= m_vga->vga.svga_intf.vram_size;

	// Clipping — derive actual pixel coordinates for the scissors test.
	int16_t check_x, check_y;
	if ((ibm8514.current_cmd & 0xe000) == 0xc000 ||
		(ibm8514.current_cmd & 0xe000) == 0xe000)
	{
		// BitBLT / Pattern Fill: dest_x/dest_y are the *starting* coordinates
		// and are not updated per-pixel during the blit loop.  Derive the
		// actual destination pixel position from the VRAM offset instead.
		uint32_t line_len = IBM8514_LINE_LENGTH;
		if (line_len == 0) line_len = 1;  // safety
		check_y = (int16_t)(dest_offset / line_len);
		check_x = (int16_t)((dest_offset % line_len) / (ibm8514.color_bpp + 1));
	}
	else
	{
		check_x = ibm8514.curr_x;
		check_y = ibm8514.curr_y;
	}
	if (check_x < ibm8514.scissors_left || check_x > ibm8514.scissors_right ||
		check_y < ibm8514.scissors_top || check_y > ibm8514.scissors_bottom)
		return;  // clipped

	// Select source color and mix mode 
	uint16_t mix_reg = use_fgmix ? ibm8514.fgmix : ibm8514.bgmix;
	uint8_t sel = (mix_reg >> 5) & 3;        // bits 6-5: CLR-SRC
	uint8_t mix_mode = mix_reg & 0x0f;       // bits 3-0: MIX type
	uint32_t src_dat = 0, dst_dat = 0;

	switch (sel)
	{
	case 0:  // Background Color register
		src_dat = ibm8514.bgcolour;
		break;
	case 1:  // Foreground Color register
		src_dat = ibm8514.fgcolour;
		break;
	case 2:  // CPU data (pixel transfer register)
	{
		// In "through plane" mode, use the full pixel value from pixel_xfer
		// In "across plane" mode, the bit extraction already happened in ibm8514_write()
		// so by the time we get here in CPU-through-plane, the pixel_xfer IS the color.
		ibm8514.pixel_xfer = pixtrans_set_endian(ibm8514.pixel_xfer, ibm8514.bus_size, 
			ibm8514.color_bpp, ibm8514.current_cmd);
		uint32_t lane = (ibm8514.curr_x >= ibm8514.prev_x) ? 
		                (ibm8514.curr_x - ibm8514.prev_x) : 
		                (ibm8514.prev_x - ibm8514.curr_x);
		src_dat = pixtrans_lane_u32(ibm8514.pixel_xfer, ibm8514.bus_size, ibm8514.color_bpp, lane);
		break;
	}
	case 3:  // Display memory (VRAM at source coords)
		src_offset %= m_vga->vga.svga_intf.vram_size;
		switch (ibm8514.color_bpp) {
			case 0:
				src_dat = m_vga->mem_linear_r(src_offset);
				break;
			case 1:
				src_dat = ((m_vga->mem_linear_r(src_offset)) |
					(m_vga->mem_linear_r(src_offset + 1) << 8));
				break;
			default:
				src_dat = ((m_vga->mem_linear_r(src_offset)) |
					(m_vga->mem_linear_r(src_offset + 1) << 8) |
					(m_vga->mem_linear_r(src_offset + 2) << 16) |
					(m_vga->mem_linear_r(src_offset + 3) << 24));
				break;
		}
		break;
	}

	// Read destination 
	switch (ibm8514.color_bpp) {
		case 0:
			dst_dat = m_vga->mem_linear_r(dest_offset);
			break;
		case 1:
			dst_dat = ((m_vga->mem_linear_r(dest_offset)) |
				(m_vga->mem_linear_r(dest_offset + 1) << 8));
			break;
		default:
			dst_dat = ((m_vga->mem_linear_r(dest_offset)) |
				(m_vga->mem_linear_r(dest_offset + 1) << 8) |
				(m_vga->mem_linear_r(dest_offset + 2) << 16) |
				(m_vga->mem_linear_r(dest_offset + 3) << 24));
			break;
	}

	// Step 4: Color Compare gate (Trio64: 2-mode, tests SOURCE)
	if (ibm8514.color_cmp_enabled)
	{
		bool match = (src_dat == ibm8514.color_cmp);
		// SRC NE = 0: write only when source != compare (skip when match)
		// SRC NE = 1: write only when source == compare (skip when no match)
		if (ibm8514.color_cmp_src_ne ? !match : match)
			return;  // color compare rejects this pixel
	}

	// Apply MIX
	uint32_t result = ibm8514_mix(mix_mode, src_dat, dst_dat);

	// Step 6: Write Mask merge
	uint32_t wrt_mask = ibm8514.write_mask;
	result = (result & wrt_mask) | (dst_dat & ~wrt_mask);

	// Step 7: Write to VRAM
	switch (ibm8514.color_bpp) {
		case 0:
			m_vga->mem_linear_w(dest_offset, result);
			break;
		case 1:
			m_vga->mem_linear_w(dest_offset, result);
			m_vga->mem_linear_w(dest_offset + 1, result >> 8);
			break;
		default:
			m_vga->mem_linear_w(dest_offset, result);
			m_vga->mem_linear_w(dest_offset + 1, result >> 8);
			m_vga->mem_linear_w(dest_offset + 2, result >> 16);
			m_vga->mem_linear_w(dest_offset + 3, result >> 24);
			break;
	}
}


uint16_t ibm8514a_device::ibm8514_color_cmp_r()
{
	return ibm8514.color_cmp & 0xffff;
}

void ibm8514a_device::ibm8514_color_cmp_w(uint16_t data)
{
	ibm8514.color_cmp = (ibm8514.color_cmp & 0xffff0000) | data;
	LOG("8514/A: Color Compare write (Low) %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_color_cmp_r_hi()
{
	return (ibm8514.color_cmp >> 16);
}

void ibm8514a_device::ibm8514_color_cmp_w_hi(uint16_t data)
{
	ibm8514.color_cmp = (ibm8514.color_cmp & 0x0000ffff) | (data << 16);
	LOG("8514/A: Color Compare write (High) %04x\n", data);
}

//es40

void ibm8514a_device::ibm8514_write_fg(uint32_t offset)
{
	uint32_t src_offset = (ibm8514.curr_y * IBM8514_LINE_LENGTH) + (ibm8514.curr_x * (ibm8514.color_bpp + 1));
	ibm8514_do_pixel(offset, src_offset, true);
}

void ibm8514a_device::ibm8514_write_bg(uint32_t offset)
{
	uint32_t src_offset = (ibm8514.curr_y * IBM8514_LINE_LENGTH) + (ibm8514.curr_x * (ibm8514.color_bpp + 1));
	ibm8514_do_pixel(offset, src_offset, false);
}

void ibm8514a_device::ibm8514_write(uint32_t offset, uint32_t src)
{
	int data_size = 8;
	uint32_t xfer;

	switch (ibm8514.pixel_control & 0x00c0)
	{
	case 0x0000:  // Foreground Mix only
		ibm8514_write_fg(offset);
		break;
	case 0x0040:  // fixed pattern (?)
		// TODO
		break;
	case 0x0080:  // use pixel transfer register
		if (ibm8514.bus_size == 0)  // 8-bit
			data_size = 8;
		if (ibm8514.bus_size == 1)  // 16-bit
			data_size = 16;
		if (ibm8514.bus_size >= 2)  // 32-bit
			data_size = 32;
		xfer = ibm8514.pixel_xfer;
		if ((ibm8514.current_cmd & 0x1000) && (data_size != 8)) {
			if (data_size == 16) {
				xfer = ((xfer & 0x00ff) << 8) | ((xfer & 0xff00) >> 8);
			}
			else if (data_size == 32) {
				xfer = ((xfer & 0x000000ff) << 24) |
					((xfer & 0x0000ff00) << 8) |
					((xfer & 0x00ff0000) >> 8) |
					((xfer & 0xff000000) >> 24);
			}
		}
		if (ibm8514.current_cmd & 0x0002)
		{
			// Mono expand: bit order depends on CMD bit 3 (0x0008).
			// DECwindows uses CMD=0x55B3 a lot, which has 0x0008 set.
			//  - 0x0008 clear: MSB-first
			//  - 0x0008 set:   LSB-first
			uint32_t bitpos;
			if (ibm8514.current_cmd & 0x0008) {
				bitpos = ibm8514.src_x;                     // LSB-first
			}
			else {
				bitpos = (data_size - 1u) - ibm8514.src_x;  // MSB-first
			}
			const uint32_t mask = 1u << bitpos;
			if (xfer & mask)
				ibm8514_write_fg(offset);
			else
				ibm8514_write_bg(offset);
		}
		else
		{
			ibm8514_write_fg(offset);
		}
		ibm8514.src_x++;
		if (ibm8514.src_x >= data_size)
			ibm8514.src_x = 0;
		break;
	case 0x00c0:  // use source plane
	{
		uint32_t srcpix;
		// In Mix Select==11 mode the source plane (VRAM) selects between FG/BG mix.
		// The Read Mask register chooses which bit(s) in the source byte are examined.
		switch (ibm8514.color_bpp) {
			case 0:
				srcpix = m_vga->mem_linear_r(src % m_vga->vga.svga_intf.vram_size);
				break;
			case 1:
				srcpix = ((m_vga->mem_linear_r(src % m_vga->vga.svga_intf.vram_size)) |
					(m_vga->mem_linear_r((src + 1) % m_vga->vga.svga_intf.vram_size) << 8));
				break;
			default:
				srcpix = ((m_vga->mem_linear_r(src % m_vga->vga.svga_intf.vram_size)) |
					(m_vga->mem_linear_r((src + 1) % m_vga->vga.svga_intf.vram_size) << 8) |
					(m_vga->mem_linear_r((src + 2) % m_vga->vga.svga_intf.vram_size) << 16) |
					(m_vga->mem_linear_r((src + 3) % m_vga->vga.svga_intf.vram_size) << 24));
				break;
		}
		uint32_t readmask = ibm8514.read_mask;  // foreground only when all bits are set
		const bool use_fg = ((srcpix & readmask) == readmask);
		if (use_fg) ibm8514_write_fg(offset);
		else        ibm8514_write_bg(offset);
		break;
	}
	}
}

/*
92E8h W(R/W):  Line Error Term Read/Write Register (ERR_TERM).
bit  0-12  (911/924) LINE PARAMETER/ERROR TERM. For Line Drawing this is the
			Bresenham Initial Error Term 2*dminor-dmajor (one less if the
			starting X is less than the ending X) in two's complement format.
			(dminor is the length of the line projected onto the minor or
			dependent axis, dmajor is the length of the line projected onto
			the major or independent axis).
	 0-13  (80x +) LINE PARAMETER/ERROR TERM. See above.
 */
uint16_t ibm8514a_device::ibm8514_line_error_r()
{
	return ibm8514.line_errorterm;
}

void ibm8514a_device::ibm8514_line_error_w(uint16_t data)
{
	ibm8514.line_errorterm = data;
	LOG("8514/A: Line Parameter/Error Term write %04x\n", data);
}

/*
  9AE8h W(R):  Graphics Processor Status Register (GP_STAT)
bit   0-7  Queue State.
			 00h = 8 words available - queue is empty
			 01h = 7 words available
			 03h = 6 words available
			 07h = 5 words available
			 0Fh = 4 words available
			 1Fh = 3 words available
			 3Fh = 2 words available
			 7Fh = 1 word  available
			 FFh = 0 words available - queue is full
		8  (911-928) DTA AVA. Read Data Available. If set data is ready to be
			read from the PIX_TRANS register (E2E8h).
		9  HDW BSY. Hardware Graphics Processor Busy
		   If set the Graphics Processor is busy.
	   10  (928 +) AE. All FIFO Slots Empty. If set all FIFO slots are empty.
	11-15  (864/964) (R) Queue State bits 8-12. 1Fh if 8 words or less
			available, Fh for 9 words, 7 for 10 words, 3 for 11 words, 1 for
			12 words and 0 for 13 words available.
 */
uint16_t ibm8514a_device::ibm8514_gpstatus_r()
{
	uint16_t ret = 0x0000;

	// A WAIT command stays busy until its full COUNT of PIX_TRANS data (fifo_idx bytes) arrives.
	if (ibm8514.gpbusy || ibm8514.fifo_idx > 0)
		ret |= 0x0200;  // bit 9: HDW BSY
	else
		ret |= 0x0400;  // bit 10: AE (All FIFO Slots Empty)

	if (ibm8514.data_avail)
		ret |= 0x0100;  // bit 8: DTA AVA (data available)

	return ret;
}

void ibm8514a_device::ibm8514_draw_vector(uint16_t len, uint8_t dir, bool draw)
{
	uint32_t offset;
	int x = 0;
	bool last_pxof = (ibm8514.current_cmd & 0x04) != 0;  // CMD bit 2

	while (x <= len)
	{
		// skip last pixel if LAST_PXOF set
		if (draw && !(last_pxof && x == len))
		{
			offset = (ibm8514.curr_y * IBM8514_LINE_LENGTH) + (ibm8514.curr_x * (ibm8514.color_bpp + 1));
			ibm8514_write(offset, offset);
		}

		switch (dir)
		{
		case 0:  // 0 degrees
			ibm8514.curr_x++;
			break;
		case 1:  // 45 degrees
			ibm8514.curr_x++;
			ibm8514.curr_y--;
			break;
		case 2:  // 90 degrees
			ibm8514.curr_y--;
			break;
		case 3:  // 135 degrees
			ibm8514.curr_y--;
			ibm8514.curr_x--;
			break;
		case 4:  // 180 degrees
			ibm8514.curr_x--;
			break;
		case 5:  // 225 degrees
			ibm8514.curr_x--;
			ibm8514.curr_y++;
			break;
		case 6:  // 270 degrees
			ibm8514.curr_y++;
			break;
		case 7:  // 315 degrees
			ibm8514.curr_y++;
			ibm8514.curr_x++;
			break;
		}
		x++;
	}
}

// es40 specific
// PIX_TRANS bytes a WAIT command consumes (S3 DB014-B 13.3.3.2/.4/.5 COUNT)
// Every line starts with a fresh bus-width write.
// Through the plane each write carries whole pixels across the plane (CMD bit 1) one mask bit per pixel.
static int wait_xfer_bytes(uint16_t cmd, uint8_t color_bpp, int width, int height)
{
	const int bus_bits = ((cmd >> 9) & 3) == 0 ? 8 : ((cmd >> 9) & 3) == 1 ? 16 : 32;
	const int pixel_bits = (cmd & 0x0002) ? 1 : (color_bpp == 0 ? 8 : color_bpp == 1 ? 16 : 32);
	return (width * pixel_bits + bus_bits - 1) / bus_bits * height * (bus_bits / 8);
}

// end es40 specific

/*
9AE8h W(W):  Drawing Command Register (CMD)
bit     0  (911-928) ~RD/WT. Read/Write Data. If set VRAM write operations are
			enabled. If clear operations execute normally but writes are
			disabled.
		1  PX MD. Pixel Mode. Defines the orientation of the display bitmap.
			 0 = Through plane mode (Single pixel transferred at a time)
			 1 = Across plane mode (Multiple pixels transferred at a time).
		2  LAST PXOF. Last Pixel Off. If set the last pixel of a line command
		   (CMD_LINE, SSV or LINEAF) is not drawn. This is used for mixes such
		   as XOR where drawing the same pixel twice would give the wrong
		   color.
		3  DIR TYP. Direction Type.
			 0: Bresenham line drawing (X-Y Axial)
				  CMD_LINE draws a line using the Bresenham algorithm as
				  specified in the DESTY_AXSTP (8AE8h), DESTX_DIASTP (8EE8h),
				  ERR_TERM (92E8h) and MAJ_AXIS_PCNT (96E8h) registers
				  INC_X, INC_Y and YMAJAXIS determines the direction.
			 1: Vector line draws (Radial).
				  CMD_NOP allows drawing of Short Stroke Vectors (SSVs) by
				  writing to the Short Stroke register (9EE8h).
				  CMD_LINE draws a vector of length MAJ_AXIS_PCNT (96E8h)
				  in the direction specified by LINEDIR (bits 5-7).
				  DRWG-DIR determines the direction of the line.
		4  DRAW YES. If clear the current position is moved, but no pixels
		   are modified. This bit should be set when attempting read or
		   write of bitmap data.
	  5-7  DRWG-DIR. Drawing Direction. When a line draw command (CMD_LINE)
		   with DIR TYP=1 (Radial) is issued, these bits define the direction
		   of the line counter clockwise relative to the positive X-axis.
			 0 = 000 degrees
			 1 = 045 degrees
			 2 = 090 degrees
			 3 = 135 degrees
			 4 = 180 degrees
			 5 = 225 degrees
			 6 = 270 degrees
			 7 = 315 degrees
		5  INC_X. This bit together with INC_Y determines which quadrant
		   the slope of a line lies within. They also determine the
		   orientation of rectangle draw commands.
		   If set lines are drawn in the positive X direction (left to right).
		6  YMAJAXIS. For Bresenham line drawing commands this bit determines
		   which axis is the independent or major axis. INC_X and INC_Y
		   determines which quadrant the slope falls within. This bit further
		   defines the slope to within an octant.
		   If set Y is the major (independent) axis.
		7  INC_Y. This bit together with INC_X determines which quadrant
		   the slope of a line lies within. They also determine the
		   orientation of rectangle draw commands.
		   If set lines are drawn in the positive Y direction (down).
		8  WAIT YES. If set the drawing engine waits for read/write of the
		   PIX_TRANS register (E2E8h) for each pixel during a draw operation.
		9  (911-928) BUS SIZE. If set the PIX_TRANS register (E2E8h) is
			processed internally as two bytes in the order specified by BYTE
			SWAP. If clear all accesses to E2E8h are 8bit.
	 9-10  (864,964) BUS SIZE. Select System Bus Size. Controls the width of
			the Pixel Data Transfer registers (E2E8h,E2EAh) and the memory
			mapped I/O. 0: 8bit, 1: 16bit, 2: 32bit
	   12  BYTE SWAP. Affects both reads and writes of SHORT_STROKE (9EE8h)
		   and PIX_TRANS (E2E8h) when 16bit=1.
		   If set take low byte first, if clear take high byte first.
	13-15  Draw Command:
			0 = NOP. Used for Short Stroke Vectors.
			1 = Draw Line. If bit 3 is set the line is drawn to the angle in
				bits 5-7 and the length in the Major Axis Pixel Count register
				(96E8h), if clear the line is drawn from the Bresenham
				constants in the Axial Step Constant register(8AE8h), Diagonal
				Step Constant register (8EE8h), Line Error Term register
			   (92E8h) and bits 5-7 of this register.
			2 = Rectangle Fill. The Current X (86E8h) and Y (82E8h)
				registers holds the coordinates of the rectangle to fill and
				the Major Axis Pixel Count register (96E8h) holds the
				horizontal width (in pixels) fill and the Minor Axis Pixel
				Count register (BEE8h index 0) holds the height of the
				rectangle.
			6 = BitBLT. Copies the source rectangle specified by the Current X
				(86E8h) and Y (8AE8h) registers to the destination rectangle,
				specified as for the Rectangle Fills.
			7 = (80x +) Pattern Fill. The source rectangle is an 8x8 pattern
				rectangle, which is copied repeatably to the destination
				rectangle.
 */
void ibm8514a_device::ibm8514_cmd_w(uint16_t data)
{
	int x, y;
	int pattern_x, pattern_y;
	uint32_t off, src;
	uint8_t readmask;

	ibm8514.current_cmd = data;
	ibm8514.src_x = 0;
	ibm8514.src_y = 0;
	ibm8514.bus_size = (data & 0x0600) >> 9;

	ibm8514.fifo_idx = 0;  // a new command abandons any unfinished CPU transfer

	switch (data & 0xe000)
	{
	case 0x0000:  // NOP (for "Short Stroke Vectors")
		ibm8514.state = IBM8514_IDLE;
		ibm8514.gpbusy = false;
		LOG("8514/A: Command (%04x) - NOP (Short Stroke Vector)\n", ibm8514.current_cmd);
		break;
	case 0x2000:  // Line
		ibm8514.state = IBM8514_IDLE;
		ibm8514.gpbusy = false;
		if (data & 0x0100)  // textured line: MAJ_AXIS_PCNT + 1 pixels, or the 2-point line's span
			ibm8514.fifo_idx = wait_xfer_bytes(data, ibm8514.color_bpp, (data & 0x0800)
				? std::max(std::abs(ibm8514.dest_x - ibm8514.curr_x), std::abs(ibm8514.dest_y - ibm8514.curr_y)) + 1
				: (ibm8514.rect_width & 0x0fff) + 1, 1);
		if (data & 0x0008)
		{
			if (data & 0x0100)
			{
				ibm8514.state = IBM8514_DRAWING_LINE;
				ibm8514.data_avail = true;
				LOG("8514/A: Command (%04x) - Vector Line (WAIT) %i,%i \n", ibm8514.current_cmd, ibm8514.curr_x, ibm8514.curr_y);
			}
			else
			{
				ibm8514_draw_vector(ibm8514.rect_width, (data & 0x00e0) >> 5, (data & 0x0010) ? true : false);
				LOG("8514/A: Command (%04x) - Vector Line - %i,%i \n", ibm8514.current_cmd, ibm8514.curr_x, ibm8514.curr_y);
			}
		}
		else if (data & 0x0800)
		{
			// ============================================================
			// Trio64 CMD type 1001: Polyline / 2-Point Line
			// (S3 Trio64 datasheet Section 13.3.3.11)
			//
			// Draws from (curr_x, curr_y) to (dest_x, dest_y).
			// The hardware auto-computes direction, major axis, and
			// Bresenham parameters from the endpoints.
			//
			// Register 8AE8h = ending Y coordinate (dest_y)
			// Register 8EE8h = ending X coordinate (dest_x)
			// ============================================================

			int32_t x0 = (int16_t)ibm8514.curr_x;
			int32_t y0 = (int16_t)ibm8514.curr_y;
			int32_t x1 = (int16_t)ibm8514.dest_x;
			int32_t y1 = (int16_t)ibm8514.dest_y;

			int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
			int32_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
			int sx = (x1 >= x0) ? 1 : -1;
			int sy = (y1 >= y0) ? 1 : -1;

			bool steep = (dy > dx);
			int count = steep ? dy : dx;
			int err;
			bool last_pxof = (data & 0x04) != 0;

			if (steep)
				err = 2 * dx - dy;
			else
				err = 2 * dy - dx;

			LOG("8514/A: Command (%04x) - Polyline/2-Point Line from %i,%i to %i,%i  "
				"dx=%i dy=%i count=%i steep=%d\n",
				ibm8514.current_cmd, x0, y0, x1, y1, dx, dy, count, steep);

			for (int i = 0; i <= count; i++)
			{
				ibm8514.curr_x = (int16_t)(x0 & 0xfff);
				ibm8514.curr_y = (int16_t)(y0 & 0xfff);

				if ((data & 0x0010) && !(last_pxof && i == count))
				{
					uint32_t offset = ((uint32_t)(y0 & 0xfff) * IBM8514_LINE_LENGTH)
						+ ((uint32_t)(x0 & 0xfff) * (ibm8514.color_bpp + 1));
					ibm8514_write(offset, offset);
				}

				if (steep)
				{
					if (err >= 0) { x0 += sx; err -= 2 * dy; }
					err += 2 * dx;
					y0 += sy;
				}
				else
				{
					if (err >= 0) { y0 += sy; err -= 2 * dx; }
					err += 2 * dy;
					x0 += sx;
				}
			}

			ibm8514.curr_x = (int16_t)(x0 & 0xfff);
			ibm8514.curr_y = (int16_t)(y0 & 0xfff);
		}
		else
		{
			// ============================================================
			// CMD type 0001: Draw Line (traditional Bresenham)
			//
			// Uses pre-programmed step constants:
			//   8AE8h = Axial Step Constant (14-bit signed)
			//   8EE8h = Diagonal Step Constant (14-bit signed)
			//   92E8h = Error Term (14-bit signed)
			//   96E8h = Major Axis Pixel Count
			// Direction from CMD bits: INC_X (5), YMAJAXIS (6), INC_Y (7)
			// ============================================================

			int16_t err = ibm8514.line_errorterm;
			int16_t axial_step = ibm8514.line_axial_step;
			int16_t diag_step = ibm8514.line_diagonal_step;
			int count = ibm8514.rect_width;
			bool last_pxof = (data & 0x04) != 0;

			// Sign-extend 14-bit register values to 16-bit
			if (err & 0x2000) err |= 0xC000;
			if (axial_step & 0x2000) axial_step |= 0xC000;
			if (diag_step & 0x2000) diag_step |= 0xC000;

			// Direction bits
			int sx = (data & 0x0020) ? 1 : -1;
			int sy = (data & 0x0080) ? 1 : -1;
			bool y_major = (data & 0x0040) != 0;

			int32_t cx = ibm8514.curr_x;
			if (cx >= 0x800) cx |= ~0x7ff;
			int32_t cy = ibm8514.curr_y;
			if (cy >= 0x800) cy |= ~0x7ff;

			LOG("8514/A: Command (%04x) - Bresenham Line - %i,%i  "
				"Axial %i, Diagonal %i, Error %i, Count %i\n",
				ibm8514.current_cmd, cx, cy, axial_step, diag_step, err, count);

			for (int i = 0; i <= count; i++)
			{
				ibm8514.curr_x = cx & 0xfff;
				ibm8514.curr_y = cy & 0xfff;

				if ((data & 0x0010) && !(last_pxof && i == count))
				{
					uint32_t offset = ((cy & 0xfff) * IBM8514_LINE_LENGTH) + ((cx & 0xfff) * (ibm8514.color_bpp + 1));
					ibm8514_write(offset, offset);
				}

				if (err >= 0)
				{
					err += diag_step;
					if (y_major) cx += sx; else cy += sy;
				}
				else
				{
					err += axial_step;
				}

				if (y_major) cy += sy; else cx += sx;
			}



			// Update position registers
			ibm8514.curr_x = cx & 0xfff;
			ibm8514.curr_y = cy & 0xfff;
			}
		break;
	case 0x4000:  // Rectangle Fill
		if (data & 0x0100)  // WAIT (for read/write of PIXEL TRANSFER (E2E8))
		{
			ibm8514.state = IBM8514_DRAWING_RECT;
			//ibm8514.gpbusy = true;  // DirectX 5 keeps waiting for the busy bit to be clear...
			ibm8514.fifo_idx = wait_xfer_bytes(data, ibm8514.color_bpp,
				(ibm8514.rect_width & 0x0fff) + 1, (ibm8514.rect_height & 0x0fff) + 1);
			ibm8514.bus_size = (data & 0x0600) >> 9;
			ibm8514.data_avail = true;
			LOG("8514/A: Command (%04x) - Rectangle Fill (WAIT) %i,%i Width: %i Height: %i Colour: %08x\n",
				ibm8514.current_cmd, ibm8514.curr_x, ibm8514.curr_y, ibm8514.rect_width, ibm8514.rect_height, ibm8514.fgcolour);
			break;
		}
		LOG("8514/A: Command (%04x) - Rectangle Fill %i,%i Width: %i Height: %i Colour: %08x\n",
			ibm8514.current_cmd, ibm8514.curr_x, ibm8514.curr_y, ibm8514.rect_width, ibm8514.rect_height, ibm8514.fgcolour);
		off = 0;
		off += (IBM8514_LINE_LENGTH * ibm8514.curr_y);
		off += (ibm8514.curr_x * (ibm8514.color_bpp + 1));
		for (y = 0; y <= ibm8514.rect_height; y++)
		{
			for (x = 0; x <= ibm8514.rect_width; x++)
			{
				if (data & 0x0020)  // source pattern is always based on current X/Y?
					ibm8514_write((off + x * (ibm8514.color_bpp + 1)) % m_vga->vga.svga_intf.vram_size, 
						(off + x * (ibm8514.color_bpp + 1)) % m_vga->vga.svga_intf.vram_size);
				else
					ibm8514_write((off - x * (ibm8514.color_bpp + 1)) % m_vga->vga.svga_intf.vram_size, 
						(off - x * (ibm8514.color_bpp + 1)) % m_vga->vga.svga_intf.vram_size);
				if (ibm8514.current_cmd & 0x0020)
				{
					ibm8514.curr_x++;
					if (ibm8514.curr_x > ibm8514.prev_x + ibm8514.rect_width)
					{
						ibm8514.curr_x = ibm8514.prev_x;
						ibm8514.src_x = 0;
						if (ibm8514.current_cmd & 0x0080)
							ibm8514.curr_y++;
						else
							ibm8514.curr_y--;
					}
				}
				else
				{
					ibm8514.curr_x--;
					if (ibm8514.curr_x < ibm8514.prev_x - ibm8514.rect_width)
					{
						ibm8514.curr_x = ibm8514.prev_x;
						ibm8514.src_x = 0;
						if (ibm8514.current_cmd & 0x0080)
							ibm8514.curr_y++;
						else
							ibm8514.curr_y--;
					}
				}
			}
			if (data & 0x0080)
				off += IBM8514_LINE_LENGTH;
			else
				off -= IBM8514_LINE_LENGTH;
		}
		ibm8514.state = IBM8514_IDLE;
		ibm8514.gpbusy = false;
		break;
	case 0xc000:  // BitBLT
		// TODO: a10cuba sets up blantantly invalid parameters here, CPU core bug maybe?
		LOG("8514/A: Command (%04x) - BitBLT from %i,%i to %i,%i  Width: %i  Height: %i\n",
			ibm8514.current_cmd, ibm8514.curr_x, ibm8514.curr_y, ibm8514.dest_x, ibm8514.dest_y, ibm8514.rect_width, ibm8514.rect_height);
		off = 0;
		off += (IBM8514_LINE_LENGTH * ibm8514.dest_y);
		off += (ibm8514.dest_x * (ibm8514.color_bpp + 1));
		src = 0;
		src += (IBM8514_LINE_LENGTH * ibm8514.curr_y);
		src += (ibm8514.curr_x * (ibm8514.color_bpp + 1));
		readmask = ((ibm8514.read_mask & 0x01) << 7) | ((ibm8514.read_mask & 0xfe) >> 1);
		for (y = 0; y <= ibm8514.rect_height; y++)
		{
			for (x = 0; x <= ibm8514.rect_width; x++)
			{
				if (data & 0x0020) ibm8514_write(off + x * (ibm8514.color_bpp + 1), src + x * (ibm8514.color_bpp + 1));
				else               ibm8514_write(off - x * (ibm8514.color_bpp + 1), src - x * (ibm8514.color_bpp + 1));

				if (ibm8514.current_cmd & 0x0020)
				{
					ibm8514.curr_x++;
					if (ibm8514.curr_x > ibm8514.prev_x + ibm8514.rect_width)
					{
						ibm8514.curr_x = ibm8514.prev_x;
						ibm8514.src_x = 0;
						if (ibm8514.current_cmd & 0x0080)
							ibm8514.curr_y++;
						else
							ibm8514.curr_y--;
					}
				}
				else
				{
					ibm8514.curr_x--;
					if (ibm8514.curr_x < ibm8514.prev_x - ibm8514.rect_width)
					{
						ibm8514.curr_x = ibm8514.prev_x;
						ibm8514.src_x = 0;
						if (ibm8514.current_cmd & 0x0080)
							ibm8514.curr_y++;
						else
							ibm8514.curr_y--;
					}
				}
			}
			if (data & 0x0080)
			{
				src += IBM8514_LINE_LENGTH;
				off += IBM8514_LINE_LENGTH;
			}
			else
			{
				src -= IBM8514_LINE_LENGTH;
				off -= IBM8514_LINE_LENGTH;
			}
		}
		ibm8514.state = IBM8514_IDLE;
		ibm8514.gpbusy = false;
		ibm8514.curr_x = ibm8514.prev_x;
		ibm8514.curr_y = ibm8514.prev_y;
		break;
	case 0xe000:  // Pattern Fill
		LOG("8514/A: Command (%04x) - Pattern Fill - source %i,%i  dest %i,%i  Width: %i Height: %i\n",
			ibm8514.current_cmd, ibm8514.curr_x, ibm8514.curr_y, ibm8514.dest_x, ibm8514.dest_y, ibm8514.rect_width, ibm8514.rect_height);
		off = 0;
		off += (IBM8514_LINE_LENGTH * ibm8514.dest_y);
		off += (ibm8514.dest_x * (ibm8514.color_bpp + 1));
		src = 0;
		src += (IBM8514_LINE_LENGTH * ibm8514.curr_y);
		src += (ibm8514.curr_x * (ibm8514.color_bpp + 1));

		pattern_x = ibm8514.dest_x & 7;
		pattern_y = ibm8514.dest_y & 7;
		ibm8514.curr_x += pattern_x;
		ibm8514.curr_y += pattern_y;
		src += (IBM8514_LINE_LENGTH * pattern_y);

		for (y = 0; y <= ibm8514.rect_height; y++)
		{
			for (x = 0; x <= ibm8514.rect_width; x++)
			{
				if (data & 0x0020)
				{
					ibm8514_write(off + x * (ibm8514.color_bpp + 1), src + pattern_x * (ibm8514.color_bpp + 1));
					pattern_x++;
					ibm8514.curr_x++;
					if (pattern_x >= 8) {
						ibm8514.curr_x = ibm8514.prev_x;
						pattern_x = 0;
					}
				}
				else
				{
					ibm8514_write(off - x * (ibm8514.color_bpp + 1), src - pattern_x * (ibm8514.color_bpp + 1));
					pattern_x--;
					ibm8514.curr_x--;
					if (pattern_x < 0) {
						ibm8514.curr_x = ibm8514.prev_x;
						pattern_x = 7;
					}
				}
			}

			// for now, presume that INC_X and INC_Y affect both src and dest, at is would for a bitblt.
			pattern_x = ibm8514.dest_x & 7;  // restore pattern pointer to the start point
			ibm8514.curr_x = ibm8514.prev_x + pattern_x;
			if (data & 0x0080)
			{
				pattern_y++;
				ibm8514.curr_y++;
				src += IBM8514_LINE_LENGTH;
				if (pattern_y >= 8)
				{
					pattern_y = 0;
					ibm8514.curr_y = ibm8514.prev_y;
					src -= (IBM8514_LINE_LENGTH * 8);  // move src pointer back to top of pattern
				}
				off += IBM8514_LINE_LENGTH;
			}
			else
			{
				pattern_y--;
				ibm8514.curr_y--;
				src -= IBM8514_LINE_LENGTH;
				if (pattern_y < 0)
				{
					pattern_y = 7;
					ibm8514.curr_y = ibm8514.prev_y;
					src += (IBM8514_LINE_LENGTH * 8);  // move src pointer back to bottom of pattern
				}
				off -= IBM8514_LINE_LENGTH;
			}
		}
		ibm8514.state = IBM8514_IDLE;
		ibm8514.gpbusy = false;
		ibm8514.curr_x = ibm8514.prev_x;
		ibm8514.curr_y = ibm8514.prev_y;
		break;
	default:
		ibm8514.state = IBM8514_IDLE;
		ibm8514.gpbusy = false;
		LOG("8514/A: Unknown command: %04x\n", data);
		break;
	}
}

/*
8AE8h W(R/W):  Destination Y Position & Axial Step Constant Register
			   (DESTY_AXSTP)
bit  0-11  DESTINATION Y-POSITION. During BITBLT operations this is the Y
		   co-ordinate of the destination in pixels.
	 0-12  (911/924) LINE PARAMETER AXIAL STEP CONSTANT. During Line Drawing,
			this is the Bresenham constant 2*dminor in two's complement
			format. (dminor is the length of the line projected onto the minor
			or dependent axis).
	 0-13  (80 x+) LINE PARAMETER AXIAL STEP CONSTANT. Se above

 */
uint16_t ibm8514a_device::ibm8514_desty_r()
{
	return ibm8514.line_axial_step;
}

void ibm8514a_device::ibm8514_desty_w(uint16_t data)
{
	ibm8514.line_axial_step = data & 0x3fff;
	ibm8514.dest_y = data & 0x0fff;
	LOG("8514/A: Line Axial Step / Destination Y write %04x\n", data);
}

/*
8EE8h W(R/W):  Destination X Position & Diagonal Step Constant Register
			   (DESTX_DISTP)
bit  0-11  DESTINATION X-POSITION. During BITBLT operations this is the X
		   co-ordinate of the destination in pixels.
	 0-12  (911/924) LINE PARAMETER DIAGONAL STEP CONSTANT. During Line
			Drawing this is the Bresenham constant 2*dminor-2*dmajor in two's
			complement format. (dminor is the length of the line projected
			onto the minor or dependent axis, dmajor is the length of the line
			projected onto the major or independent axis)
	 0-13  (80x +) LINE PARAMETER DIAGONAL STEP CONSTANT. Se above

 */
uint16_t ibm8514a_device::ibm8514_destx_r()
{
	return ibm8514.line_diagonal_step;
}

void ibm8514a_device::ibm8514_destx_w(uint16_t data)
{
	ibm8514.line_diagonal_step = data & 0x3fff;
	ibm8514.dest_x = data & 0x0fff;
	LOG("8514/A: Line Diagonal Step / Destination X write %04x\n", data);
}

/*
9EE8h W(R/W):  Short Stroke Vector Transfer Register (SHORT_STROKE)
bit   0-3  Length of vector projected onto the major axis.
		   This is also the number of pixels drawn.
		4  Must be set for pixels to be written.
	  5-7  VECDIR. The angle measured counter-clockwise from horizontal
		   right) at which the line is drawn,
			 0 = 000 degrees
			 1 = 045 degrees
			 2 = 090 degrees
			 3 = 135 degrees
			 4 = 180 degrees
			 5 = 225 degrees
			 6 = 270 degrees
			 7 = 315 degrees
	 8-15  The lower 8 bits are duplicated in the upper 8 bits so two
		   short stroke vectors can be drawn with one command.
Note: The upper byte must be written for the SSV command to be executed.
	  Thus if a byte is written to 9EE8h another byte must be written to
	  9EE9h before execution starts. A single 16bit write will do.
	  If only one SSV is desired the other byte can be set to 0.
 */
void ibm8514a_device::ibm8514_wait_draw_ssv()
{
	uint8_t len = ibm8514.wait_vector_len;
	uint8_t dir = ibm8514.wait_vector_dir;
	bool draw = ibm8514.wait_vector_draw;
	uint8_t count = ibm8514.wait_vector_count;
	uint32_t offset;
	int x;
	int data_size;

	switch (ibm8514.bus_size)
	{
	case 0:
		data_size = 8;
		break;
	case 1:
		data_size = 16;
		break;
	default:
		data_size = 32;
		break;
	}
	
	int loop_inc = (ibm8514.current_cmd & 0x02) ? 1 : 8;

	for (x = 0; x < data_size; x += loop_inc)
	{
		if (len > count)
		{
			if (ibm8514.state == IBM8514_DRAWING_SSV_1)
			{
				ibm8514.state = IBM8514_DRAWING_SSV_2;
				ibm8514.wait_vector_len = (ibm8514.ssv & 0x0f00) >> 8;
				ibm8514.wait_vector_dir = (ibm8514.ssv & 0xe000) >> 13;
				ibm8514.wait_vector_draw = (ibm8514.ssv & 0x1000) ? true : false;
				ibm8514.wait_vector_count = 0;
				return;
			}
			else if (ibm8514.state == IBM8514_DRAWING_SSV_2)
			{
				ibm8514.state = IBM8514_IDLE;
				ibm8514.gpbusy = false;
				ibm8514.data_avail = false;
				return;
			}
		}

		if (ibm8514.state == IBM8514_DRAWING_SSV_1 || ibm8514.state == IBM8514_DRAWING_SSV_2)
		{
			offset = (ibm8514.curr_y * IBM8514_LINE_LENGTH) + (ibm8514.curr_x * (ibm8514.color_bpp + 1));
			if (draw)
				ibm8514_write(offset, offset);
			switch (dir)
			{
			case 0:  // 0 degrees
				ibm8514.curr_x++;
				break;
			case 1:  // 45 degrees
				ibm8514.curr_x++;
				ibm8514.curr_y--;
				break;
			case 2:  // 90 degrees
				ibm8514.curr_y--;
				break;
			case 3:  // 135 degrees
				ibm8514.curr_y--;
				ibm8514.curr_x--;
				break;
			case 4:  // 180 degrees
				ibm8514.curr_x--;
				break;
			case 5:  // 225 degrees
				ibm8514.curr_x--;
				ibm8514.curr_y++;
				break;
			case 6:  // 270 degrees
				ibm8514.curr_y++;
				break;
			case 7:  // 315 degrees
				ibm8514.curr_y++;
				ibm8514.curr_x++;
				break;
			}
		}
	}
}

void ibm8514a_device::ibm8514_draw_ssv(uint8_t data)
{
	uint8_t len = data & 0x0f;
	uint8_t dir = (data & 0xe0) >> 5;
	bool draw = (data & 0x10) ? true : false;

	ibm8514_draw_vector(len, dir, draw);
}

uint16_t ibm8514a_device::ibm8514_ssv_r()
{
	return ibm8514.ssv;
}

void ibm8514a_device::ibm8514_ssv_w(uint16_t data)
{
	ibm8514.ssv = data;

	if (ibm8514.current_cmd & 0x0100)
	{
		ibm8514.state = IBM8514_DRAWING_SSV_1;
		ibm8514.data_avail = true;
		ibm8514.wait_vector_len = ibm8514.ssv & 0x0f;
		ibm8514.wait_vector_dir = (ibm8514.ssv & 0xe0) >> 5;
		ibm8514.wait_vector_draw = (ibm8514.ssv & 0x10) ? true : false;
		ibm8514.wait_vector_count = 0;
		return;
	}

	if (ibm8514.current_cmd & 0x1000)  // byte sequence
	{
		ibm8514_draw_ssv(data & 0xff);
		ibm8514_draw_ssv(data >> 8);
	}
	else
	{
		ibm8514_draw_ssv(data >> 8);
		ibm8514_draw_ssv(data & 0xff);
	}
	LOG("8514/A: Short Stroke Vector write %04x\n", data);
}

void ibm8514a_device::ibm8514_wait_draw_vector()
{
	uint8_t len = ibm8514.wait_vector_len;
	uint8_t dir = ibm8514.wait_vector_dir;
	bool draw = ibm8514.wait_vector_draw;
	uint8_t count = ibm8514.wait_vector_count;
	uint32_t offset;
	uint8_t data_size = 0;
	int x;

	if (ibm8514.bus_size == 0)  // 8-bit
		data_size = 8;
	if (ibm8514.bus_size == 1)  // 16-bit
		data_size = 16;
	if (ibm8514.bus_size >= 2)  // 32-bit
		data_size = 32;

	int loop_inc = (ibm8514.current_cmd & 0x02) ? 1 : 8;

	for (x = 0; x < data_size; x += loop_inc)
	{
		if (len > count)
		{
			if (ibm8514.state == IBM8514_DRAWING_LINE)
			{
				ibm8514.state = IBM8514_IDLE;
				ibm8514.gpbusy = false;
				ibm8514.data_avail = false;
				return;
			}
		}

		if (ibm8514.state == IBM8514_DRAWING_LINE)
		{
			offset = (ibm8514.curr_y * IBM8514_LINE_LENGTH) + (ibm8514.curr_x * (ibm8514.color_bpp + 1));
			if (draw)
				ibm8514_write(offset, offset);
			switch (dir)
			{
			case 0:  // 0 degrees
				ibm8514.curr_x++;
				break;
			case 1:  // 45 degrees
				ibm8514.curr_x++;
				ibm8514.curr_y--;
				break;
			case 2:  // 90 degrees
				ibm8514.curr_y--;
				break;
			case 3:  // 135 degrees
				ibm8514.curr_y--;
				ibm8514.curr_x--;
				break;
			case 4:  // 180 degrees
				ibm8514.curr_x--;
				break;
			case 5:  // 225 degrees
				ibm8514.curr_x--;
				ibm8514.curr_y++;
				break;
			case 6:  // 270 degrees
				ibm8514.curr_y++;
				break;
			case 7:  // 315 degrees
				ibm8514.curr_y++;
				ibm8514.curr_x++;
				break;
			}
		}
	}
}

/*
96E8h W(R/W):  Major Axis Pixel Count/Rectangle Width Register (MAJ_AXIS_PCNT)
bit  0-10  (911/924)  RECTANGLE WIDTH/LINE PARAMETER MAX. For BITBLT and
			rectangle commands this is the width of the area. For Line Drawing
			this is the Bresenham constant dmajor in two's complement format.
			(dmajor is the length of the line projected onto the major or
			independent axis). Must be positive.
	 0-11  (80x +) RECTANGLE WIDTH/LINE PARAMETER MAX. See above
 */
uint16_t ibm8514a_device::ibm8514_width_r()
{
	return ibm8514.rect_width;
}

void ibm8514a_device::ibm8514_width_w(uint16_t data)
{
	ibm8514.rect_width = data & 0x1fff;
	LOG("8514/A: Major Axis Pixel Count / Rectangle Width write %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_currentx_r()
{
	return ibm8514.curr_x;
}

void ibm8514a_device::ibm8514_currentx_w(uint16_t data)
{
	ibm8514.curr_x = data;
	ibm8514.prev_x = data;
	LOG("8514/A: Current X set to %04x (%i)\n", data, ibm8514.curr_x);
}

uint16_t ibm8514a_device::ibm8514_currenty_r()
{
	return ibm8514.curr_y;
}

void ibm8514a_device::ibm8514_currenty_w(uint16_t data)
{
	ibm8514.curr_y = data;
	ibm8514.prev_y = data;
	LOG("8514/A: Current Y set to %04x (%i)\n", data, ibm8514.curr_y);
}

uint16_t ibm8514a_device::ibm8514_fgcolour_r()
{
	return ibm8514.fgcolour & 0xffff;
}

void ibm8514a_device::ibm8514_fgcolour_w(uint16_t data)
{
	ibm8514.fgcolour = (ibm8514.fgcolour & 0xffff0000) | data;
	LOG("8514/A: Foreground Colour (Low) write %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_fgcolour_r_hi()
{
	return (ibm8514.fgcolour >> 16);
}

void ibm8514a_device::ibm8514_fgcolour_w_hi(uint16_t data)
{
	ibm8514.fgcolour = (ibm8514.fgcolour & 0x0000ffff) | (data << 16);
	LOG("8514/A: Foreground Colour (High) write %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_bgcolour_r()
{
	return ibm8514.bgcolour & 0xffff;
}

void ibm8514a_device::ibm8514_bgcolour_w(uint16_t data)
{
	ibm8514.bgcolour = (ibm8514.bgcolour & 0xffff0000) | data;
	LOG("8514/A: Background Colour (Low) write %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_bgcolour_r_hi()
{
	return (ibm8514.bgcolour >> 16);
}

void ibm8514a_device::ibm8514_bgcolour_w_hi(uint16_t data)
{
	ibm8514.bgcolour = (ibm8514.bgcolour & 0x0000ffff) | (data << 16);
	LOG("8514/A: Background Colour (High) write %04x\n", data);
}

/*
AEE8h W(R/W):  Read Mask Register (RD_MASK)
bit   0-7  (911/924) Read Mask affects the following commands: CMD_RECT,
			CMD_BITBLT and reading data in Across Plane Mode.
			Each bit set prevents the plane from being read.
	 0-15  (801/5) Readmask. See above.
	 0-31  (928 +) Readmask. See above. In 32 bits per pixel modes there are
			two 16bit registers at this address. BEE8h index 0Eh bit 4 selects
			which 16 bits are accessible and each access toggles to the other
			16 bits.
 */
uint16_t ibm8514a_device::ibm8514_read_mask_r()
{
	return ibm8514.read_mask & 0xffff;
}

void ibm8514a_device::ibm8514_read_mask_w(uint16_t data)
{
	ibm8514.read_mask = (ibm8514.read_mask & 0xffff0000) | data;
	LOG("8514/A: Read Mask (Low) write = %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_read_mask_r_hi()
{
	return (ibm8514.read_mask >> 16);
}

void ibm8514a_device::ibm8514_read_mask_w_hi(uint16_t data)
{
	ibm8514.read_mask = (ibm8514.read_mask & 0x0000ffff) | (data << 16);
	LOG("8514/A: Read Mask (High) write = %04x\n", data);
}

/*
AAE8h W(R/W):  Write Mask Register (WRT_MASK)
bit   0-7  (911/924) Writemask. A plane can only be modified if the
			corresponding bit is set.
	 0-15  (801/5) Writemask. See above.
	 0-31  (928 +) Writemask. See above. In 32 bits per pixel modes there are
			two 16bit registers at this address. BEE8h index 0Eh bit 4 selects
			which 16 bits are accessible and each access toggles to the other
			16 bits.
 */
uint16_t ibm8514a_device::ibm8514_write_mask_r()
{
	return ibm8514.write_mask & 0xffff;
}

void ibm8514a_device::ibm8514_write_mask_w(uint16_t data)
{
	ibm8514.write_mask = (ibm8514.write_mask & 0xffff0000) | data;
	LOG("8514/A: Write Mask (Low) write = %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_write_mask_r_hi()
{
	return (ibm8514.write_mask >> 16);
}

void ibm8514a_device::ibm8514_write_mask_w_hi(uint16_t data)
{
	ibm8514.write_mask = (ibm8514.write_mask & 0x0000ffff) | (data << 16);
	LOG("8514/A: Read Mask (High) write = %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_multifunc_r(uint16_t setup_control)
{
	uint16_t ret = 0;

	switch (ibm8514.multifunc_sel)
	{
	case 0:  ret = ibm8514.rect_height; break;
	case 1:  ret = ibm8514.scissors_top; break;
	case 2:  ret = ibm8514.scissors_left; break;
	case 3:  ret = ibm8514.scissors_bottom; break;
	case 4:  ret = ibm8514.scissors_right; break;
	case 5:  ret = ibm8514.pixel_control; break;           // Index 0Ah
	case 6:  ret = ibm8514.multifunc_misc; break;           // Index 0Eh
	case 7:  ret = ibm8514.current_cmd & 0x1fff; break;    // 9AE8h, bits 15-13 forced to 0
	case 8:  ret = ibm8514.substatus & 0x0fff; break;      // 42E8h, bits 15-12 forced to 0
	case 9:  ret = setup_control & 0x18; break;          // Video Subsystem Enable
	case 10: ret = ibm8514.multifunc_misc2; break;          // Index 0Dh
	default: ret = 0xff; break;
	}

	// Auto-increment: each read of BEE8h advances the index
	ibm8514.multifunc_sel = (ibm8514.multifunc_sel + 1) % 11;

	return ret;
}

void ibm8514a_device::ibm8514_multifunc_w(uint16_t data)
{
	// array for readback support
	uint8_t idx = (data >> 12) & 0x0f;
	ibm8514.multifunc[idx] = data & 0x0fff;

	switch (data & 0xf000)
	{
		/*
		BEE8h index 00h W(R/W): Minor Axis Pixel Count Register (MIN_AXIS_PCNT).
		bit  0-10  (911/924) Rectangle Height. Height of BITBLT or rectangle command.
					Actual height is one larger.
			 0-11  (80x +) Rectangle Height. See above
		*/
	case 0x0000:
		ibm8514.rect_height = data & 0x0fff;
		LOG("8514/A: Multifunction minor axis pixel count / rect height write %04x\n", data);
		break;
		/*
		BEE8h index 01h W(R/W):  Top Scissors Register (SCISSORS_T).
		bit  0-10  (911/924) Clipping Top Limit. Defines the upper bound of the
					Clipping Rectangle (Lowest Y coordinate).
			 0-11  (80x +) Clipping Top Limit. See above

		BEE8h index 02h W(R/W):  Left Scissors Registers (SCISSORS_L).
		bit  0-10  (911,924) Clipping Left Limit. Defines the left bound of the
					Clipping Rectangle (Lowest X coordinate).
			 0-11  (80x +) Clipping Left Limit. See above.

		BEE8h index 03h W(R/W):  Bottom Scissors Register (SCISSORS_B).
		bit  0-10  (911,924) Clipping Bottom Limit. Defines the bottom bound of the
					Clipping Rectangle (Highest Y coordinate).
			 0-11  (80x +) Clipping Bottom Limit. See above.

		BEE8h index 04h W(R/W):  Right Scissors Register (SCISSORS_R).
		bit  0-10  (911,924) Clipping Right Limit. Defines the right bound of the
					Clipping Rectangle (Highest X coordinate).
			 0-11  (80x +) Clipping Bottom Limit. See above.
		 */
	case 0x1000:
		ibm8514.scissors_top = data & 0x0fff;
		LOG("8514/A: Scissors Top write %04x\n", data);
		break;
	case 0x2000:
		ibm8514.scissors_left = data & 0x0fff;
		LOG("8514/A: Scissors Left write %04x\n", data);
		break;
	case 0x3000:
		ibm8514.scissors_bottom = data & 0x0fff;
		LOG("8514/A: Scissors Bottom write %04x\n", data);
		break;
	case 0x4000:
		ibm8514.scissors_right = data & 0x0fff;
		LOG("8514/A: Scissors Right write %04x\n", data);
		break;
		/*
		BEE8h index 0Ah W(R/W):  Pixel Control Register (PIX_CNTL).
		BIT     2  (911-928) Pack Data. If set image read data is a monochrome bitmap,
					if clear it is a bitmap of the current pixel depth
			  6-7  DT-EX-DRC. Select Mix Select.
					 0  Foreground Mix is always used.
					 1  use fixed pattern to decide which mix setting to use on a pixel
					 2  CPU Data (Pixel Transfer register) determines the Mix register used.
					 3  Video memory determines the Mix register used.
		 */
	case 0xa000:
		ibm8514.pixel_control = data;
		LOG("8514/A: Pixel control write %04x\n", data);
		break;
	case 0xd000:
	{
		// BEE8h Index 0Dh: MULT_MISC2 - Source/Destination base address (MB granularity)
		ibm8514.multifunc_misc2 = data & 0x0fff;
		uint8_t dst_mb = data & 0x07;
		uint8_t src_mb = (data >> 4) & 0x07;
		ibm8514.dst_base = (uint32_t)dst_mb * 1048576u;
		ibm8514.src_base = (uint32_t)src_mb * 1048576u;
		LOG("8154/A: MULT_MISC2 write %04x (dst_base=%uMB, src_base=%uMB)\n",
			data, dst_mb, src_mb);
		break;
	}
	case 0xe000:
		ibm8514.multifunc_misc = data;
		// Extract Color Compare control bits (Trio64 datasheet Section 18)
		ibm8514.color_cmp_src_ne = (data >> 7) & 1;  // bit 7: SRC NE
		ibm8514.color_cmp_enabled = (data >> 8) & 1;  // bit 8: enable color compare
		LOG("8154/A: Multifunction Misc write %04x (color_cmp_en=%d, src_ne=%d)\n",
			data, ibm8514.color_cmp_enabled, ibm8514.color_cmp_src_ne);
		break;
		/*
		BEE8h index 0Fh W(W):  Read Register Select Register (READ_SEL)    (801/5,928)
		bit   0-2  (911-928) READ-REG-SEL. Read Register Select. Selects the register
					that is actually read when a read of BEE8h happens. Each read of
					BEE8h increments this register by one.
					 0: Read will return contents of BEE8h index 0.
					 1: Read will return contents of BEE8h index 1.
					 2: Read will return contents of BEE8h index 2.
					 3: Read will return contents of BEE8h index 3.
					 4: Read will return contents of BEE8h index 4.
					 5: Read will return contents of BEE8h index 0Ah.
					 6: Read will return contents of BEE8h index 0Eh.
					 7: Read will return contents of 9AE8h (Bits 13-15 will be 0).
			  0-3  (864,964) READ-REG-SEL. See above plus:
					 8: Read will return contents of 42E8h (Bits 12-15 will be 0)
					 9: Read will return contents of 46E8h
					10: Read will return contents of BEE8h index 0Dh
		 */
	case 0xf000:
		ibm8514.multifunc_sel = data & 0x000f;
		LOG("8154/A: Multifunction select write %04x\n", data);
		break;
	default:
		LOG("8154/A: Unimplemented multifunction register %i write %03x\n", data >> 12, data & 0x0fff);
		break;
	}
}

void ibm8514a_device::ibm8514_wait_draw()
{
	int x, data_size = 8;
	uint32_t off;

	// the data in the pixel transfer register or written to VRAM masks the rectangle output
	if (ibm8514.bus_size == 0)  // 8-bit
		data_size = 8;
	if (ibm8514.bus_size == 1)  // 16-bit
		data_size = 16;
	if (ibm8514.bus_size >= 2)  // 32-bit
		data_size = 32;
	off = 0;
	off += (IBM8514_LINE_LENGTH * ibm8514.curr_y);
	off += (ibm8514.curr_x * (ibm8514.color_bpp + 1));
	if (ibm8514.current_cmd & 0x02) // "across plane mode"
	{
		for (x = 0; x < data_size; x++)
		{
			ibm8514_write(off, off);
			if (ibm8514.current_cmd & 0x0020)
			{
				ibm8514.curr_x++;
				off += (ibm8514.color_bpp + 1);
				if (ibm8514.curr_x > ibm8514.prev_x + ibm8514.rect_width)
				{
					ibm8514.curr_x = ibm8514.prev_x;
					ibm8514.src_x = 0;
					if (ibm8514.current_cmd & 0x0080)
					{
						ibm8514.curr_y++;
						if (ibm8514.curr_y > ibm8514.prev_y + ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.data_avail = false;
							ibm8514.gpbusy = false;
						}
					}
					else
					{
						ibm8514.curr_y--;
						if (ibm8514.curr_y < ibm8514.prev_y - ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.data_avail = false;
							ibm8514.gpbusy = false;
						}
					}
					return;
				}
			}
			else
			{
				ibm8514.curr_x--;
				off -= (ibm8514.color_bpp + 1);
				if (ibm8514.curr_x < ibm8514.prev_x - ibm8514.rect_width)
				{
					ibm8514.curr_x = ibm8514.prev_x;
					ibm8514.src_x = 0;
					if (ibm8514.current_cmd & 0x0080)
					{
						ibm8514.curr_y++;
						if (ibm8514.curr_y > ibm8514.prev_y + ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.gpbusy = false;
							ibm8514.data_avail = false;
						}
					}
					else
					{
						ibm8514.curr_y--;
						if (ibm8514.curr_y < ibm8514.prev_y - ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.gpbusy = false;
							ibm8514.data_avail = false;
						}
					}
					return;
				}
			}
		}
	}
	else
	{
		// "through plane" mode (single pixel)
		int pixel = (ibm8514.color_bpp + 1) * 8;
		for (x = 0; x < data_size; x = x + pixel)
		{
			ibm8514_write(off, off);

			if (ibm8514.current_cmd & 0x0020)
			{
				ibm8514.curr_x++;
				off += (ibm8514.color_bpp + 1);
				if (ibm8514.curr_x > ibm8514.prev_x + ibm8514.rect_width)
				{
					ibm8514.curr_x = ibm8514.prev_x;
					ibm8514.src_x = 0;
					if (ibm8514.current_cmd & 0x0080)
					{
						ibm8514.curr_y++;
						if (ibm8514.curr_y > ibm8514.prev_y + ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.gpbusy = false;
							ibm8514.data_avail = false;
						}
					}
					else
					{
						ibm8514.curr_y--;
						if (ibm8514.curr_y < ibm8514.prev_y - ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.gpbusy = false;
							ibm8514.data_avail = false;
						}
					}
					return;
				}
			}
			else
			{
				ibm8514.curr_x--;
				off -= (ibm8514.color_bpp + 1);
				if (ibm8514.curr_x < ibm8514.prev_x - ibm8514.rect_width)
				{
					ibm8514.curr_x = ibm8514.prev_x;
					ibm8514.src_x = 0;
					if (ibm8514.current_cmd & 0x0080)
					{
						ibm8514.curr_y++;
						if (ibm8514.curr_y > ibm8514.prev_y + ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.gpbusy = false;
							ibm8514.data_avail = false;
						}
					}
					else
					{
						ibm8514.curr_y--;
						if (ibm8514.curr_y < ibm8514.prev_y - ibm8514.rect_height)
						{
							ibm8514.state = IBM8514_IDLE;
							ibm8514.gpbusy = false;
							ibm8514.data_avail = false;
						}
					}
					return;
				}
			}
		}
	}
}

/*
B6E8h W(R/W):  Background Mix Register (BKGD_MIX)
bit   0-3  Background MIX (BACKMIX).
	  5-6  CLR-SRC. Color Source. See Foreground Mix.
*/
uint16_t ibm8514a_device::ibm8514_backmix_r()
{
	return ibm8514.bgmix;
}

void ibm8514a_device::ibm8514_backmix_w(uint16_t data)
{
	ibm8514.bgmix = data;
	ibm8514.bkgd_sel = (data >> 5) & 3;       // bits 6-5: color source
	ibm8514.bkgd_mix_mode = data & 0x0f;       // bits 3-0: mix type
	LOG("8514/A: Background Mix write %04x (sel=%d, mix=%d)\n", data, ibm8514.bkgd_sel, ibm8514.bkgd_mix_mode);
}

/*
BAE8h W(R/W):  Foreground Mix Register (FRGD_MIX)
bit   0-3  Foreground MIX (FOREMIX).
	  5-6  CLR-SRC. Color Source.
		   0 = Background Color register
		   1 = Foreground Color register
		   2 = Pixel Data from CPU
		   3 = Display Memory (bitmap data)
*/
uint16_t ibm8514a_device::ibm8514_foremix_r()
{
	return ibm8514.fgmix;
}

void ibm8514a_device::ibm8514_foremix_w(uint16_t data)
{
	ibm8514.fgmix = data;
	ibm8514.frgd_sel = (data >> 5) & 3;       // bits 6-5: color source
	ibm8514.frgd_mix_mode = data & 0x0f;       // bits 3-0: mix type
	LOG("8514/A: Foreground Mix write %04x (sel=%d, mix=%d)\n", data, ibm8514.frgd_sel, ibm8514.frgd_mix_mode);
}

uint16_t ibm8514a_device::ibm8514_pixel_xfer_r(offs_t offset)
{
	if (offset == 1)
		return (ibm8514.pixel_xfer & 0xffff0000) >> 16;
	else
		return ibm8514.pixel_xfer & 0x0000ffff;
}

void ibm8514a_device::ibm8514_pixel_xfer_w(offs_t offset, uint16_t data)
{
	ibm8514.fifo_idx = std::max(ibm8514.fifo_idx - 2, 0);
	if (offset == 1)
		ibm8514.pixel_xfer = (ibm8514.pixel_xfer & 0x0000ffff) | (data << 16);
	else
		ibm8514.pixel_xfer = (ibm8514.pixel_xfer & 0xffff0000) | data;

	if (ibm8514.state == IBM8514_DRAWING_RECT)
		ibm8514_wait_draw();

	if (ibm8514.state == IBM8514_DRAWING_SSV_1 || ibm8514.state == IBM8514_DRAWING_SSV_2)
		ibm8514_wait_draw_ssv();

	if (ibm8514.state == IBM8514_DRAWING_LINE)
		ibm8514_wait_draw_vector();

	LOG("8514/A: Pixel Transfer = %08x\n", ibm8514.pixel_xfer);
}

void ibm8514a_device::ibm8514_pixel_xfer_complete()
{
	if (ibm8514.state == IBM8514_DRAWING_RECT)
		ibm8514_wait_draw();
	else if (ibm8514.state == IBM8514_DRAWING_SSV_1 ||
		ibm8514.state == IBM8514_DRAWING_SSV_2)
		ibm8514_wait_draw_ssv();
	else if (ibm8514.state == IBM8514_DRAWING_LINE)
		ibm8514_wait_draw_vector();
}

/*
02E8h W(R):  Display Status Register
bit     0  SENSE is the result of a wired-OR of 3 comparators, one
		   for each of the RGB video signal.
		   By programming the RAMDAC for various values
		   and patterns and then reading the SENSE, the monitor type
		   (color, monochrome or none) can be determined.
		1  VBLANK. Vertical Blank State
		   If Vertical Blank is active this bit is set.
		2  HORTOG. Horizontal Toggle
		   This bit toggles every time a HSYNC pulse starts
	 3-15  Reserved(0)
 */
uint8_t ibm8514a_device::ibm8514_status_r(offs_t offset)
{
	switch (offset)
	{
	case 0:
		return m_vga->vga_vblank() << 1;
	case 2:
		return m_vga->ramdac_mask_r(0);
	case 3:
		return m_vga->ramdac_state_r(0);
	case 4:
		return m_vga->ramdac_write_index_r(0);
	case 5:
		return m_vga->ramdac_data_r(0);
	}
	return 0;
}

void ibm8514a_device::ibm8514_htotal_w(offs_t offset, uint8_t data)
{
	switch (offset)
	{
	case 0:
		ibm8514.htotal = data & 0xff;
		break;
	case 2:
		m_vga->ramdac_mask_w(0, data);
		break;
	case 3:
		m_vga->ramdac_read_index_w(0, data);
		break;
	case 4:
		m_vga->ramdac_write_index_w(0, data);
		break;
	case 5:
		m_vga->ramdac_data_w(0, data);
		break;
	}
	//vga.crtc.horz_total = data & 0x01ff;
	LOG("8514/A: Horizontal total write %04x\n", data);
}

/*
42E8h W(R):  Subsystem Status Register (SUBSYS_STAT)
bit   0-3  Interrupt requests. These bits show the state of internal interrupt
		   requests. An interrupt will only occur if the corresponding bit(s)
		   in SUBSYS_CNTL is set. Interrupts can only be reset by writing a 1
		   to the corresponding Interrupt Clear bit in SUBSYS_CNTL.
			 Bit 0: VBLNKFLG
				 1: PICKFLAG
				 2: INVALIDIO
				 3: GPIDLE
	  4-6  MONITORID.
			  1: IBM 8507 (1024x768) Monochrome
			  2: IBM 8514 (1024x768) Color
			  5: IBM 8503 (640x480) Monochrome
			  6: IBM 8512/13 (640x480) Color
		7  8PLANE.
		   (CT82c480) This bit is latched on reset from pin P4D7.
	 8-11  CHIP_REV. Chip revision number.
	12-15  (CT82c480) CHIP_ID. 0=CT 82c480.
 */
uint16_t ibm8514a_device::ibm8514_substatus_r()
{
	// TODO:
	if (m_vga->vga_vblank() != 0)  // not correct, but will do for now
		ibm8514.substatus |= 0x01;
	return ibm8514.substatus;
}

/*
42E8h W(W):  Subsystem Control Register (SUBSYS_CNTL)
bit   0-3  Interrupt Reset. Write 1 to a bit to reset the interrupt.
		   Bit 0  RVBLNKFLG   Write 1 to reset Vertical Blank interrupt.
			   1  RPICKFLAG   Write 1 to reset PICK interrupt.
			   2  RINVALIDIO  Write 1 to reset Queue Overflow/Data
							  Underflow interrupt.
			   3  RGPIDLE     Write 1 to reset GPIDLE interrupt.
	  4-7  Reserved(0)
		8  IBLNKFLG.   If set Vertical Blank Interrupts are enabled.
		9  IPICKFLAG.  If set PICK Interrupts are enabled.
	   10  IINVALIDIO. If set Queue Overflow/Data Underflow Interrupts are
					   enabled.
	   11  IGPIDLE.    If set Graphics Engine Idle Interrupts are enabled.
	12-13  CHPTEST. Used for chip testing.
	14-15  Graphics Processor Control (GPCTRL).
 */
void ibm8514a_device::ibm8514_subcontrol_w(uint16_t data, uint16_t mem_mask)
{
	// SUBSYS_CNTL is write-only; subctrl is the internal latch.
	ibm8514.subctrl = (ibm8514.subctrl & ~mem_mask) | (data & mem_mask);
	ibm8514.substatus &= ~(data & mem_mask & 0x0f);
	//  LOG("8514/A: Subsystem control write %04x\n", data);
	if ((mem_mask & 0xc000) == 0xc000 && (data & 0xc000) == 0x8000)
	{
		// Cancel the modeled in-flight operation. 
		// Clearing only the transfer count leaves GP_STAT busy and lets PIX_TRANS resume the old command.
		// DB014-B does not specify clearing the programming registers.
		ibm8514.fifo_idx = 0;
		ibm8514.gpbusy = false;
		ibm8514.data_avail = false;
		ibm8514.state = IBM8514_IDLE;
	}
}

uint16_t ibm8514a_device::ibm8514_subcontrol_r()
{
	return ibm8514.subctrl;
}

/*  22E8 (W)
 * Display Control
 *  bits 1-2: Line skip control - 0=bits 1-2 skipped, 1=bit 2 skipped
 *  bit    3: Double scan
 *  bit    4: Interlace
 *  bits 5-6: Emable Display - 0=no change, 1=enable 8514/A, 2 or 3=8514/A reset
 */
void ibm8514a_device::ibm8514_display_ctrl_w(uint16_t data)
{
	ibm8514.display_ctrl = data & 0x7e;
	switch (data & 0x60)
	{
	case 0x00:
		break;  // do nothing
	case 0x20:
		ibm8514.enabled = true;  // enable 8514/A
		break;
	case 0x40:
	case 0x60:  // reset (does this disable the 8514/A?)
		ibm8514.enabled = false;
		break;
	}
}

void ibm8514a_device::ibm8514_advfunc_w(uint16_t data)
{
	ibm8514.advfunction_ctrl = data;
	ibm8514.passthrough = data & 0x0001;
}

uint16_t ibm8514a_device::ibm8514_htotal_r()
{
	return ibm8514.htotal;
}

uint16_t ibm8514a_device::ibm8514_vtotal_r()
{
	return ibm8514.vtotal;
}

void ibm8514a_device::ibm8514_vtotal_w(uint16_t data)
{
	ibm8514.vtotal = data;
	//  vga.crtc.vert_total = data;
	LOG("8514/A: Vertical total write %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_vdisp_r()
{
	return ibm8514.vdisp;
}

void ibm8514a_device::ibm8514_vdisp_w(uint16_t data)
{
	ibm8514.vdisp = data;
	//  vga.crtc.vert_disp_end = data >> 3;
	LOG("8514/A: Vertical Displayed write %04x\n", data);
}

uint16_t ibm8514a_device::ibm8514_vsync_r()
{
	return ibm8514.vsync;
}

void ibm8514a_device::ibm8514_vsync_w(uint16_t data)
{
	ibm8514.vsync = data;
	LOG("8514/A: Vertical Sync write %04x\n", data);
}

void ibm8514a_device::enabled()
{
	ibm8514.state = IBM8514_IDLE;
	ibm8514.gpbusy = false;
}
