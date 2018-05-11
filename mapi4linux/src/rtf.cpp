/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <memory>
#include <cstring>
#include <cstdlib>
#include <kopano/platform.h>
#include "rtf.h"

namespace KC {

// Based on jtnef (TNEFUtils.java)
static const char lpPrebuf[] =
	"{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}"
	"{\\f0\\fnil \\froman \\fswiss \\fmodern \\fscript "
	"\\fdecor MS Sans SerifSymbolArialTimes New RomanCourier"
	"{\\colortbl\\red0\\green0\\blue0\n\r\\par "
	"\\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx";

struct RTFHeader {
	unsigned int ulCompressedSize;
	unsigned int ulUncompressedSize;
	unsigned int ulMagic;
	unsigned int ulChecksum;
};

unsigned int rtf_get_uncompressed_length(const char *lpData,
    unsigned int ulSize)
{
	// Check if we have a full header
	if(ulSize < sizeof(RTFHeader)) 
		return 0;
	
	// Return the size
	return ((RTFHeader *)lpData)->ulUncompressedSize;
}

// FIXME bad data can buffer overflow when ulUncompressedSize is incorrect
// lpDest should be pre-allocated to the uncompressed size
unsigned int rtf_decompress(char *lpDest, const char *lpSrc,
    unsigned int ulBufSize)
{
	auto lpHeader = reinterpret_cast<const struct RTFHeader *>(lpSrc);
	auto lpStart = lpSrc;
	unsigned int ulFlags = 0;
	unsigned int ulFlagNr = 0;
	unsigned char c1 = 0;
	unsigned char c2 = 0;
	unsigned int ulOffset = 0;
	unsigned int ulSize = 0;
	const unsigned int prebufSize = strlen(lpPrebuf);

	// Check if we have a full header
	if(ulBufSize < sizeof(RTFHeader)) 
		return 1;
	
	const unsigned int uncomp_size = le32_to_cpu(lpHeader->ulUncompressedSize);
	lpSrc += sizeof(struct RTFHeader);
	
	if (le32_to_cpu(lpHeader->ulMagic) == 0x414c454d) {
		// Uncompressed RTF
		memcpy(lpDest, lpSrc, ulBufSize - sizeof(RTFHeader));
		return 0;
	} else if (le32_to_cpu(lpHeader->ulMagic) != 0x75465a4c) {
		return 1;
	}
	// Allocate a buffer to decompress into (uncompressed size plus prebuffered data)
	std::unique_ptr<char[]> lpBuffer(new char[uncomp_size+prebufSize]);
	memcpy(lpBuffer.get(), lpPrebuf, prebufSize);

	// Start writing just after the prebuffered data
	char *lpWrite = lpBuffer.get() + prebufSize;
	while (lpWrite < lpBuffer.get() + uncomp_size + prebufSize) {
		// Get next bit from flags
		ulFlags = ulFlagNr++ % 8 == 0 ? *lpSrc++ : ulFlags >> 1;

		if (lpSrc >= lpStart + ulBufSize)
			// Reached the end of the input buffer somehow. We currently return OK
			// and the decoded data up to now.
			break;
		if (!(ulFlags & 1)) {
			*lpWrite++ = *lpSrc++;
			if (lpSrc >= lpStart + ulBufSize)
				break;
			continue;
		}
		if (lpSrc + 2 >= lpStart + ulBufSize)
			break;
		// Reference to existing data
		c1 = *lpSrc++;
		c2 = *lpSrc++;

		// Offset is first 12 bits
		ulOffset = (((unsigned int)c1) << 4) | (c2 >> 4);
		// Size is last 4 bits, plus 2 (0 and 1 are impossible, because 1 would be a literal (ie ulFlags & 1 == 0)
		ulSize = (c2 & 0xf) + 2;

		// We now have offset and size within our current 4k window. If the offset is after the
		// write pointer, then go back one window. (due to wrapping buffer)
		ulOffset = ((lpWrite - lpBuffer.get()) / 4096) * 4096 + ulOffset;
		if (ulOffset > (unsigned int)(lpWrite - lpBuffer.get()))
			ulOffset -= 4096;
		while (ulSize && lpWrite < &lpBuffer[uncomp_size+prebufSize] && ulOffset < uncomp_size + prebufSize) {
			*lpWrite++ = lpBuffer[ulOffset++];
			--ulSize;
		}
	}
	// Copy back the data without the prebuffer
	memcpy(lpDest, &lpBuffer[prebufSize], uncomp_size);
	return 0;
}

/**
 * rtf_compress - put RTF uncompressed into the "MELA" format
 */
unsigned int rtf_compress(char **dstp, unsigned int *dst_size,
    const char *src, unsigned int src_size)
{
	char *dst;
	*dst_size = src_size + sizeof(RTFHeader);
	*dstp = dst = reinterpret_cast<char *>(malloc(*dst_size));
	if (dst == nullptr)
		return 1;
	auto hdr = reinterpret_cast<RTFHeader *>(dst);
	hdr->ulUncompressedSize = src_size;
	hdr->ulCompressedSize = *dst_size;
	memcpy(&hdr->ulMagic, "MELA", 4);
	hdr->ulChecksum = 0;
	memcpy(&dst[sizeof(*hdr)], src, src_size);
	return 0;
}

} /* namespace KC */
