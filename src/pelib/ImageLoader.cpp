/*****************************************************************************/
/* ImageLoader.cpp                        Copyright (c) Ladislav Zezula 2020 */
/*---------------------------------------------------------------------------*/
/* Implementation of PE image loader                                         */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 30.05.20  1.00  Lad  Created                                              */
/*****************************************************************************/

#include <iostream>
#include <fstream>

#include "retdec/pelib/ImageLoader.h"

//-----------------------------------------------------------------------------
// Anti-headache

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::size_t;

//-----------------------------------------------------------------------------
// Static class variables

uint8_t PeLib::ImageLoader::ImageProtectionArray[16] =
{
	PELIB_PAGE_NOACCESS,
	PELIB_PAGE_EXECUTE,
	PELIB_PAGE_READONLY,
	PELIB_PAGE_EXECUTE_READ,
	PELIB_PAGE_WRITECOPY,
	PELIB_PAGE_EXECUTE_WRITECOPY,
	PELIB_PAGE_WRITECOPY,
	PELIB_PAGE_EXECUTE_WRITECOPY,
	PELIB_PAGE_NOACCESS,
	PELIB_PAGE_EXECUTE,
	PELIB_PAGE_READONLY,
	PELIB_PAGE_EXECUTE_READ,
	PELIB_PAGE_READWRITE,
	PELIB_PAGE_EXECUTE_READWRITE,
	PELIB_PAGE_READWRITE,
	PELIB_PAGE_EXECUTE_READWRITE
};

//-----------------------------------------------------------------------------
// Returns the fixed size of optional header (without Data Directories)

template <typename OPT_HDR>
uint32_t getCopySizeOfOptionalHeader()
{
	return offsetof(OPT_HDR, DataDirectory);
}

//-----------------------------------------------------------------------------
// Constructor and destructor

PeLib::ImageLoader::ImageLoader(uint32_t loaderFlags)
{
	memset(&dosHeader, 0, sizeof(PELIB_IMAGE_DOS_HEADER));
	memset(&fileHeader, 0, sizeof(PELIB_IMAGE_FILE_HEADER));
	memset(&optionalHeader, 0, sizeof(PELIB_IMAGE_OPTIONAL_HEADER));
	checkSumFileOffset = 0;
	securityDirFileOffset = 0;
	realNumberOfRvaAndSizes = 0;
	ntSignature = 0;
	ldrError = LDR_ERROR_NONE;

	// By default, set the most benevolent settings
	ssiImageAlignment32 = PELIB_PAGE_SIZE;
	sizeofImageMustMatch = false;
	ntHeadersSizeCheck = false;
	appContainerCheck = false;
	maxSectionCount = 255;
	is64BitWindows = (loaderFlags & LoaderMode64BitWindows) ? true : false;
	headerSizeCheck = false;
	loadArmImages = true;

	// Resolve version-specific restrictions
	switch(loaderMode = (loaderFlags & WindowsVerMask))
	{
		case LoaderModeWindowsXP:
			ssiImageAlignment32 = PELIB_SECTOR_SIZE;
			maxSectionCount = PE_MAX_SECTION_COUNT_XP;
			sizeofImageMustMatch = true;
			headerSizeCheck = true;
			loadArmImages = false;
			break;

		case LoaderModeWindows7:
			ssiImageAlignment32 = 1;                        // SECTOR_SIZE when the image is loaded from network media
			maxSectionCount = PE_MAX_SECTION_COUNT_7;
			ntHeadersSizeCheck = true;
			sizeofImageMustMatch = true;
			loadArmImages = false;
			break;

		case LoaderModeWindows10:
			ssiImageAlignment32 = 1;
			maxSectionCount = PE_MAX_SECTION_COUNT_7;
			ntHeadersSizeCheck = true;
			appContainerCheck = true;
			loadArmImages = true;
			break;
	}
}

PeLib::ImageLoader::~ImageLoader()
{}

//-----------------------------------------------------------------------------
// Public functions

bool PeLib::ImageLoader::relocateImage(uint64_t newImageBase)
{
	uint32_t VirtualAddress;
	uint32_t Size;
	bool result = true;

	// Only relocate the image if the image base is different
	if(newImageBase != optionalHeader.ImageBase)
	{
		// If the image was not properly mapped, don't even try.
		if(pages.size() == 0)
			return false;

		// If relocations are stripped, do nothing
		if(fileHeader.Characteristics & PELIB_IMAGE_FILE_RELOCS_STRIPPED)
			return false;

		// Windows 10 (built 10240) performs this check
		if(appContainerCheck && checkForBadAppContainer())
			return false;

		// Don't relocate 32-bit images to an address greater than 32bits
		if(optionalHeader.Magic == PELIB_IMAGE_NT_OPTIONAL_HDR32_MAGIC && (newImageBase >> 32))
			return false;

		// Change the image base in the header. This happens even if the image does not have relocations.
		// Sample: f5bae114007e5f5eb2a7e41fbd7cf4062b21e1a33e0648a07eb1e25c106bd7eb
		writeNewImageBase(newImageBase);

		// The image must have relocation directory
		if(optionalHeader.NumberOfRvaAndSizes <= PELIB_IMAGE_DIRECTORY_ENTRY_BASERELOC)
			return false;

		// The relocation data directory must be valid
		VirtualAddress = optionalHeader.DataDirectory[PELIB_IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
		Size = optionalHeader.DataDirectory[PELIB_IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
		if(VirtualAddress == 0 || Size == 0)
			return false;

		// Do not relocate images with weird or invalid relocation table
		if(!isValidImageBlock(VirtualAddress, Size))
			return false;

		// Perform relocations
		result = processImageRelocations(optionalHeader.ImageBase, newImageBase, VirtualAddress, Size);
	}

	return result;
}

uint32_t PeLib::ImageLoader::readImage(void * buffer, uint32_t rva, uint32_t bytesToRead)
{
	// If the image was properly mapped, perform an image-read operation
	if(rawFileData.size() == 0)
	   return readWriteImage(buffer, rva, bytesToRead, readFromPage);

	// If the image loader was unable to map the image, we provide fallback method
	// by translating the RVA to file offset. Note that in some cases, this methos
	// may produce unwanted results.
	// Example: If export directory is at the end of section, it will be padded by zeros by the loader,
	// but in the on-disk version, next section will follow.
	return readWriteImageFile(buffer, rva, bytesToRead, true);
}

uint32_t PeLib::ImageLoader::writeImage(void * buffer, uint32_t rva, uint32_t bytesToRead)
{
	// If the image was properly mapped, perform an image-read operation
	if(rawFileData.size() == 0)
		return readWriteImage(buffer, rva, bytesToRead, writeToPage);

	// If the image loader was unable to map the image, we provide fallback method
	// by translating the RVA to file offset.
	return readWriteImageFile(buffer, rva, bytesToRead, false);
}

uint32_t PeLib::ImageLoader::stringLength(uint32_t rva, uint32_t maxLength) const
{
	uint32_t rvaBegin = rva;
	uint32_t rvaEnd = rva + maxLength;
	uint32_t length = 0;

	// Is the image mapped OK?
	if(pages.size())
	{
		// Check the last possible address where we read
		if(rvaEnd > getSizeOfImageAligned())
			rvaEnd = getSizeOfImageAligned();

		// Is the offset within the image?
		if(rva < rvaEnd)
		{
			size_t pageIndex = rva / PELIB_PAGE_SIZE;

			// The page index must be in range
			if(pageIndex < pages.size())
			{
				while(rva < rvaEnd)
				{
					const PELIB_FILE_PAGE & page = pages[pageIndex];
					const uint8_t * dataBegin;
					const uint8_t * dataPtr;
					uint32_t rvaEndPage = (pageIndex + 1) * PELIB_PAGE_SIZE;

					// If zero page, means we found an RVA with zero
					if(page.buffer.empty())
						return rva;
					dataBegin = dataPtr = page.buffer.data() + (rva & (PELIB_PAGE_SIZE - 1));

					// Perhaps the last page loaded?
					if(rvaEndPage > rvaEnd)
						rvaEndPage = rvaEnd;

					// Try to find the zero byte on the page
					dataPtr = (const uint8_t *)memchr(dataPtr, 0, (rvaEndPage - rva));
					if(dataPtr != nullptr)
						return rva + (dataPtr - dataBegin) - rvaBegin;
					rva = rvaEndPage;

					// Move pointers
					pageIndex++;
				}
			}
		}

		// Return the length of the string
		length = (rva - rvaBegin);
	}
	else
	{
		// Recalc the file offset to RVA
		if((rva = getFileOffsetFromRva(rva)) < rawFileData.size())
		{
			const uint8_t * stringPtr = rawFileData.data() + rva;
			const uint8_t * stringEnd;

			length = rawFileData.size() - rva;

			stringEnd = (const uint8_t *)memchr(stringPtr, 0, length);
			if(stringEnd != nullptr)
				length = stringEnd - stringPtr;
		}
	}

	return length;
}

uint32_t PeLib::ImageLoader::readString(std::string & str, uint32_t rva, uint32_t maxLength)
{
	// Check the length of the string at the rva
	uint32_t length = stringLength(rva, maxLength);

	// Allocate needeed size in the string
	str.resize(length);

	// Read the string from the image
	readImage((void *)str.data(), rva, length);
	return length;
}

uint32_t PeLib::ImageLoader::readPointer(uint32_t rva, uint64_t & pointerValue)
{
	uint32_t bytesRead = 0;

	switch(getImageBitability())
	{
		case 64:
			if(readImage(&pointerValue, rva, sizeof(uint64_t)) == sizeof(uint64_t))
				return sizeof(uint64_t);
			break;

		case 32:
		{
			uint32_t pointerValue32 = 0;

			bytesRead = readImage(&pointerValue32, rva, sizeof(uint32_t));
			if(bytesRead == sizeof(uint32_t))
			{
				pointerValue = pointerValue32;
				return sizeof(uint32_t);
			}

			break;
		}
	}

	return 0;
}

uint32_t PeLib::ImageLoader::getPointerSize()  const
{
	return getImageBitability() / 8;
}

uint32_t PeLib::ImageLoader::readStringRc(std::string & str, uint32_t rva)
{
	std::vector<uint16_t> wideString;
	uint32_t bytesToRead;
	uint32_t charsRead;
	uint16_t length = 0;

	// Read the length of the string from the image
	readImage(&length, rva, sizeof(uint16_t));
	rva += sizeof(uint16_t);

	// Allocate enough space
	bytesToRead = length * sizeof(uint16_t);
	wideString.resize(length);

	// Read the entire string from the image
	charsRead = readImage(wideString.data(), rva, bytesToRead) / sizeof(uint16_t);
	str.resize(charsRead);

	// Convert the UTF-16 string to ANSI. Note that this is not the proper way to do it,
	// but it's the same way how retdec-fileinfo.exe always did it, so we keep it that way
	for(uint32_t i = 0; i < charsRead; i++)
		str[i] = wideString[i];
	return charsRead;
}

uint32_t PeLib::ImageLoader::readStringRaw(std::vector<uint8_t> & fileData, std::string & str, size_t offset, size_t maxLength, bool mustBePrintable, bool mustNotBeTooLong)
{
	size_t length = 0;

	if(offset < fileData.size())
	{
		uint8_t * stringBegin = fileData.data() + offset;
		uint8_t * stringEnd;

		// Make sure we won't read past the end of the buffer
		if((offset + maxLength) > fileData.size())
			maxLength = fileData.size() - offset;

		// Get the length of the string. Do not go beyond the maximum length
		// Note that there is no guaratee that the string is zero terminated, so can't use strlen
		// retdec-regression-tests\tools\fileinfo\bugs\issue-451-strange-section-names\4383fe67fec6ea6e44d2c7d075b9693610817edc68e8b2a76b2246b53b9186a1-unpacked
		stringEnd = (uint8_t *)memchr(stringBegin, 0, maxLength);
		if(stringEnd == nullptr)
		{
			// No zero terminator means that the string is limited by max length
			if(mustNotBeTooLong)
				return 0;
			stringEnd = stringBegin + maxLength;
		}

		// Copy the string
		length = stringEnd - stringBegin;
		str.resize(length);
		memcpy(const_cast<char *>(str.data()), stringBegin, length);

		// Ignore strings that contain non-printable chars
		if(mustBePrintable)
		{
			for(auto oneChar : str)
			{
				if(isPrintableChar(oneChar) == false)
				{
					str.clear();
					return 0;
				}
			}
		}
	}

	return length;
}

uint32_t PeLib::ImageLoader::dumpImage(const char * fileName)
{
	// Create the file for dumping
	std::ofstream fs(fileName, std::ofstream::binary);
	uint32_t bytesWritten = 0;

	if(fs.is_open())
	{
		// Allocate one page filled with zeros
		uint8_t zeroPage[PELIB_PAGE_SIZE] = {0};
		char * dataToWrite;

		// Write each page to the file
		for(auto & page : pages)
		{
			dataToWrite = (char *)(page.buffer.size() ? page.buffer.data() : zeroPage);
			fs.write(dataToWrite, PELIB_PAGE_SIZE);
			bytesWritten += PELIB_PAGE_SIZE;
		}
	}

	return bytesWritten;
}

uint32_t PeLib::ImageLoader::getImageBitability() const
{
	if(optionalHeader.Magic == PELIB_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		return 64;

	// Default: 32-bit image
		return 32;
}

uint32_t PeLib::ImageLoader::getFileOffsetFromRva(uint32_t rva) const
{
	// If we have sections loaded, then we calculate the file offset from section headers
	if(sections.size())
	{
		// Check whether the rva goes into any section
		for(auto & sectHdr : sections)
		{
			// Only if the pointer to raw data is not zero
			if(sectHdr.PointerToRawData != 0 && sectHdr.SizeOfRawData != 0)
			{
				uint32_t realPointerToRawData = sectHdr.PointerToRawData;
				uint32_t sectionRvaStart = sectHdr.VirtualAddress;
				uint32_t virtualSize = (sectHdr.VirtualSize != 0) ? sectHdr.VirtualSize : sectHdr.SizeOfRawData;

				// For multi-section images, real pointer to raw data is aligned down to sector size
				if(optionalHeader.SectionAlignment >= PELIB_PAGE_SIZE)
					realPointerToRawData = realPointerToRawData & ~(PELIB_SECTOR_SIZE - 1);
				if(optionalHeader.SectionAlignment != 0)
					sectionRvaStart = AlignToSize(sectHdr.VirtualAddress, optionalHeader.SectionAlignment);

				// Is the RVA inside that section?
				if(sectionRvaStart <= rva && rva < (sectionRvaStart + virtualSize))
				{
					// Make sure we round the pointer to raw data down to PELIB_SECTOR_SIZE.
					// In case when PointerToRawData is less than 0x200, it maps to the header!
					return realPointerToRawData + (rva - sectionRvaStart);
				}
			}
		}

		// Check if the rva goes into the header
		return (rva < optionalHeader.SizeOfHeaders) ? rva : UINT32_MAX;
	}

	// The rva maps directly to the file offset
	return rva;
}

uint32_t PeLib::ImageLoader::getFieldOffset(PELIB_MEMBER_TYPE field) const
{
	uint32_t imageBitability = getImageBitability();
	uint32_t fieldOffset;

	switch (field)
	{
		case OPTHDR_sizeof:
			return (imageBitability == 64) ? sizeof(PELIB_IMAGE_OPTIONAL_HEADER64) : sizeof(PELIB_IMAGE_OPTIONAL_HEADER32);

		case OPTHDR_NumberOfRvaAndSizes:
			fieldOffset = (imageBitability == 64) ? offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, NumberOfRvaAndSizes) : offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, NumberOfRvaAndSizes);
			return sizeof(PELIB_IMAGE_NT_SIGNATURE) + sizeof(PELIB_IMAGE_FILE_HEADER) + fieldOffset;

		case OPTHDR_DataDirectory:
			fieldOffset = (imageBitability == 64) ? offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, DataDirectory) : offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, DataDirectory);
			return sizeof(PELIB_IMAGE_NT_SIGNATURE) + sizeof(PELIB_IMAGE_FILE_HEADER) + fieldOffset;

		case OPTHDR_DataDirectory_EXPORT_Rva:
			fieldOffset = (imageBitability == 64) ? offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, DataDirectory) : offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, DataDirectory);
			return sizeof(PELIB_IMAGE_NT_SIGNATURE) + sizeof(PELIB_IMAGE_FILE_HEADER) + fieldOffset + PELIB_IMAGE_DIRECTORY_ENTRY_EXPORT * sizeof(PELIB_IMAGE_DATA_DIRECTORY);

		case OPTHDR_DataDirectory_RSRC_Rva:
			fieldOffset = (imageBitability == 64) ? offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, DataDirectory) : offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, DataDirectory);
			return sizeof(PELIB_IMAGE_NT_SIGNATURE) + sizeof(PELIB_IMAGE_FILE_HEADER) + fieldOffset + PELIB_IMAGE_DIRECTORY_ENTRY_RESOURCE * sizeof(PELIB_IMAGE_DATA_DIRECTORY);

		case OPTHDR_DataDirectory_CONFIG_Rva:
			fieldOffset = (imageBitability == 64) ? offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, DataDirectory) : offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, DataDirectory);
			return sizeof(PELIB_IMAGE_NT_SIGNATURE) + sizeof(PELIB_IMAGE_FILE_HEADER) + fieldOffset + PELIB_IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG * sizeof(PELIB_IMAGE_DATA_DIRECTORY);
	}

	return UINT32_MAX;
}

uint32_t PeLib::ImageLoader::getRealPointerToRawData(size_t sectionIndex) const
{
	if(sectionIndex >= sections.size())
		return UINT32_MAX;
	if(optionalHeader.SectionAlignment < PELIB_PAGE_SIZE)
		return sections[sectionIndex].PointerToRawData;

	return sections[sectionIndex].PointerToRawData & ~(PELIB_SECTOR_SIZE - 1);
}

uint32_t PeLib::ImageLoader::getImageProtection(uint32_t sectionCharacteristics) const
{
	uint32_t Index = 0;

	if(sectionCharacteristics & PELIB_IMAGE_SCN_MEM_EXECUTE)
		Index |= 1;

	if(sectionCharacteristics & PELIB_IMAGE_SCN_MEM_READ)
		Index |= 2;

	if(sectionCharacteristics & PELIB_IMAGE_SCN_MEM_WRITE)
		Index |= 4;

	if(sectionCharacteristics & PELIB_IMAGE_SCN_MEM_SHARED)
		Index |= 8;

	return ImageProtectionArray[Index];
}

//-----------------------------------------------------------------------------
// Manipulation with section data

void PeLib::ImageLoader::setPointerToSymbolTable(uint32_t pointerToSymbolTable)
{
	fileHeader.PointerToSymbolTable = pointerToSymbolTable;
}

void PeLib::ImageLoader::setCharacteristics(uint32_t characteristics)
{
	fileHeader.Characteristics = characteristics;
}

void PeLib::ImageLoader::setAddressOfEntryPoint(uint32_t addressOfEntryPoint)
{
	optionalHeader.AddressOfEntryPoint = addressOfEntryPoint;
}

void PeLib::ImageLoader::setSizeOfCode(uint32_t sizeOfCode, uint32_t baseOfCode)
{
	if(sizeOfCode != UINT32_MAX)
		optionalHeader.SizeOfCode = sizeOfCode;
	if(baseOfCode != UINT32_MAX)
		optionalHeader.BaseOfCode = baseOfCode;
}

void PeLib::ImageLoader::setDataDirectory(uint32_t entryIndex, uint32_t VirtualAddress, uint32_t Size)
{
	if(entryIndex < PELIB_IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
	{
		// Make sure there is enough entries
		if(entryIndex >= optionalHeader.NumberOfRvaAndSizes)
			optionalHeader.NumberOfRvaAndSizes = entryIndex + 1;

		if(VirtualAddress != UINT32_MAX)
			optionalHeader.DataDirectory[entryIndex].VirtualAddress = VirtualAddress;
		if(Size != UINT32_MAX)
			optionalHeader.DataDirectory[entryIndex].Size = Size;
	}
}

PeLib::PELIB_IMAGE_SECTION_HEADER * PeLib::ImageLoader::addSection(const char * name, uint32_t sectionSize)
{
	if(optionalHeader.FileAlignment == 0)
		return nullptr;
	if(optionalHeader.SectionAlignment == 0)
		return nullptr;
	if(sections.size() >= UINT16_MAX)
		return nullptr;

	// Calculate the new RVA and file offset
	std::uint32_t Rva = 0;
	std::uint32_t Raw = 0;
	calcNewSectionAddresses(Rva, Raw);

	// Create new section
	PELIB_SECTION_HEADER SectHdr;
	SectHdr.setName(name);
	SectHdr.setVirtualRange(Rva, AlignToSize(sectionSize, optionalHeader.SectionAlignment));
	SectHdr.setRawDataRange(Raw, AlignToSize(sectionSize, optionalHeader.FileAlignment));
	SectHdr.Characteristics = PELIB_IMAGE_SCN_MEM_WRITE | PELIB_IMAGE_SCN_MEM_READ | PELIB_IMAGE_SCN_CNT_INITIALIZED_DATA | PELIB_IMAGE_SCN_CNT_CODE;
	sections.push_back(SectHdr);

	// Return the header of the last section
	return getSectionHeader(sections.size() - 1);
}

void PeLib::ImageLoader::calcNewSectionAddresses(uint32_t & Rva, std::uint32_t & RawOffset)
{
	uint32_t NewRawOffset = optionalHeader.SizeOfHeaders;
	uint32_t NewRva = optionalHeader.SizeOfHeaders;

	for(const auto & section : sections)
	{
		if((section.VirtualAddress + section.VirtualSize) > NewRva)
			NewRva = section.VirtualAddress + section.VirtualSize;
		if((section.PointerToRawData + section.SizeOfRawData) > NewRawOffset)
			NewRawOffset = section.PointerToRawData + section.SizeOfRawData;
	}

	RawOffset = AlignToSize(NewRawOffset, optionalHeader.FileAlignment);
	Rva = AlignToSize(NewRva, optionalHeader.SectionAlignment);
}

void PeLib::ImageLoader::setSectionName(std::size_t sectionIndex, const char * newName)
{
	if(sectionIndex < sections.size())
	{
		sections[sectionIndex].setName(newName);
	}
}

void PeLib::ImageLoader::setSectionVirtualRange(size_t sectionIndex, uint32_t VirtualAddress, uint32_t VirtualSize)
{
	if(sectionIndex < sections.size())
	{
		sections[sectionIndex].setVirtualRange(VirtualAddress, VirtualSize);
	}
}

void PeLib::ImageLoader::setSectionRawDataRange(size_t sectionIndex, uint32_t PointerToRawData, uint32_t SizeOfRawData)
{
	if(sectionIndex < sections.size())
	{
		sections[sectionIndex].setRawDataRange(PointerToRawData, SizeOfRawData);
	}
}

void PeLib::ImageLoader::setSectionCharacteristics(size_t sectionIndex, uint32_t Characteristics)
{
	if(sectionIndex < sections.size())
	{
		sections[sectionIndex].Characteristics = Characteristics;
	}
}

int PeLib::ImageLoader::splitSection(size_t sectionIndex, const std::string & prevSectName, const std::string & nextSectName, uint32_t splitOffset)
{
	if(!optionalHeader.FileAlignment)
		return PeLib::ERROR_NO_FILE_ALIGNMENT;
	if(!optionalHeader.SectionAlignment)
		return PeLib::ERROR_NO_SECTION_ALIGNMENT;

	// Index needs to be in the range <0, NUMBER OF SECTIONS)
	if(sectionIndex > sections.size())
		return PeLib::ERROR_ENTRY_NOT_FOUND;

	// Offset at which the section is going to be split must be multiple of section alignment
	if(splitOffset & (getSectionAlignment() - 1))
		return PeLib::ERROR_NOT_ENOUGH_SPACE;

	// Do not allow to split if the offset of split is greater than the size of the section
	// Nor do allow the section with size 0 to be created
	if(splitOffset >= getSectionHeader(sectionIndex)->VirtualSize)
		return PeLib::ERROR_NOT_ENOUGH_SPACE;

	// Move every section located after the inserted section by one position
	sections.resize(sections.size() + 1);
	for(int i = sections.size() - 2; i >= sectionIndex + 1; --i)
		sections[i + 1] = sections[i];

	uint32_t originalSize = getSectionHeader(sectionIndex)->SizeOfRawData;

	// Setup the first of the new sections
	setSectionName(sectionIndex, prevSectName.c_str());
	setSectionRawDataRange(sectionIndex, UINT32_MAX, splitOffset);
	setSectionVirtualRange(sectionIndex, UINT32_MAX, splitOffset);

	// Setup the second of the new sections
	setSectionName(sectionIndex + 1, nextSectName.c_str());
	setSectionRawDataRange(sectionIndex + 1, sections[sectionIndex].PointerToRawData + splitOffset, originalSize - splitOffset);
	setSectionVirtualRange(sectionIndex + 1, sections[sectionIndex].VirtualAddress + splitOffset, originalSize - splitOffset);
	setSectionCharacteristics(sectionIndex + 1, PeLib::PELIB_IMAGE_SCN_MEM_WRITE | PeLib::PELIB_IMAGE_SCN_MEM_READ | PeLib::PELIB_IMAGE_SCN_CNT_INITIALIZED_DATA | PeLib::PELIB_IMAGE_SCN_CNT_CODE);
	return PeLib::ERROR_NONE;
}

void PeLib::ImageLoader::enlargeLastSection(uint32_t sizeIncrement)
{
	if(sections.size())
	{
		auto & lastSection = sections[sections.size() - 1];

		lastSection.VirtualSize = lastSection.SizeOfRawData = AlignToSize(lastSection.SizeOfRawData + sizeIncrement, getFileAlignment());
		optionalHeader.SizeOfImage = lastSection.VirtualAddress + lastSection.VirtualSize;
	}
}

int PeLib::ImageLoader::removeSection(size_t sectionIndex)
{
	if(sectionIndex >= getNumberOfSections())
		return ERROR_ENTRY_NOT_FOUND;

	const PELIB_SECTION_HEADER * pSectionHeader = getSectionHeader(sectionIndex);
	uint32_t virtualDiff = pSectionHeader->VirtualSize;
	uint32_t rawDiff = pSectionHeader->SizeOfRawData;

	for (size_t i = sectionIndex + 1; i < getNumberOfSections(); ++i)
	{
		pSectionHeader = getSectionHeader(i);

		setSectionVirtualRange(i, pSectionHeader->VirtualAddress - virtualDiff);
		setSectionRawDataRange(i, pSectionHeader->PointerToRawData - rawDiff);
	}

	sections.erase(sections.begin() + sectionIndex);
	return ERROR_NONE;
}

void PeLib::ImageLoader::makeValid()
{
	uint32_t imageBitability = getImageBitability();
	uint32_t sizeOfHeaders;
	uint32_t sizeOfImage;
	uint32_t dwOffsetDiff;
	uint32_t alignment;

	// Fix the NT signature
	ntSignature = PELIB_IMAGE_NT_SIGNATURE;    // 'PE'

	// Fix the IMAGE_FILE_HEADER
	fileHeader.Machine = (imageBitability == 64) ? PELIB_IMAGE_FILE_MACHINE_AMD64 : PELIB_IMAGE_FILE_MACHINE_I386;
	fileHeader.NumberOfSections = (uint16_t)sections.size();
	fileHeader.SizeOfOptionalHeader = getFieldOffset(OPTHDR_sizeof);
	fileHeader.Characteristics = (fileHeader.Characteristics != 0) ? fileHeader.Characteristics : PELIB_IMAGE_FILE_EXECUTABLE_IMAGE | PELIB_IMAGE_FILE_32BIT_MACHINE;

	// Fix the IMAGE_OPTIONAL_HEADER
	optionalHeader.Magic = (imageBitability == 64) ? PELIB_IMAGE_NT_OPTIONAL_HDR64_MAGIC : PELIB_IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	optionalHeader.NumberOfRvaAndSizes = PELIB_IMAGE_NUMBEROF_DIRECTORY_ENTRIES;

	alignment = AlignToSize(optionalHeader.SectionAlignment, PELIB_PAGE_SIZE);
	optionalHeader.SectionAlignment = (alignment != 0) ? alignment : PELIB_PAGE_SIZE;
	
	alignment = AlignToSize(optionalHeader.FileAlignment, PELIB_SECTOR_SIZE);
	optionalHeader.FileAlignment = (alignment != 0) ? alignment : PELIB_SECTOR_SIZE;
	
	sizeOfHeaders = dosHeader.e_lfanew + sizeof(PELIB_IMAGE_NT_SIGNATURE) + sizeof(PELIB_IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader + fileHeader.NumberOfSections * sizeof(PELIB_IMAGE_SECTION_HEADER);
	optionalHeader.SizeOfHeaders = sizeOfHeaders = AlignToSize(sizeOfHeaders, optionalHeader.FileAlignment);

	sizeOfImage = AlignToSize(optionalHeader.SizeOfHeaders, optionalHeader.SectionAlignment);
	dwOffsetDiff = sizeOfHeaders - getSectionHeader(0)->PointerToRawData;
	for(uint16_t i = 0; i < fileHeader.NumberOfSections; i++)
	{
		const PELIB_SECTION_HEADER * pSectionHeader = getSectionHeader(i);
		
		sizeOfImage += AlignToSize(pSectionHeader->VirtualSize, optionalHeader.SectionAlignment);

		// If the size of headers changed, we need to move all section data further
		if(dwOffsetDiff)
			setSectionRawDataRange(i, pSectionHeader->PointerToRawData + dwOffsetDiff);
	}

	// Fixup the size of image
	optionalHeader.SizeOfImage = AlignToSize(sizeOfImage, optionalHeader.SectionAlignment);
}

//-----------------------------------------------------------------------------
// Loader error

int PeLib::ImageLoader::setLoaderError(PeLib::LoaderError ldrErr)
{
	// Do not override existing loader error
	if(ldrError == LDR_ERROR_NONE)
	{
		ldrError = ldrErr;
	}
	return ERROR_NONE;
}

PeLib::LoaderError PeLib::ImageLoader::loaderError() const
{
	return ldrError;
}

//-----------------------------------------------------------------------------
// Interface for loading files

int PeLib::ImageLoader::Load(std::vector<uint8_t> & fileData, bool loadHeadersOnly)
{
	int fileError;

	// Check and capture DOS header
	fileError = captureDosHeader(fileData);
	if(fileError != ERROR_NONE)
		return fileError;

	// Check and capture NT headers
	fileError = captureNtHeaders(fileData);
	if(fileError != ERROR_NONE)
		return fileError;

	// Check and capture section headers
	fileError = captureSectionHeaders(fileData);
	if(fileError != ERROR_NONE)
		return fileError;

	// Shall we map the image content?
	if(loadHeadersOnly == false)
	{
		// Large amount of memory may be allocated during loading the image to memory.
		// We need to handle low memory condition carefully here
		try
		{
			// If there was no detected image error, map the image as if Windows loader would do
			if(isImageLoadable())
			{
				fileError = captureImageSections(fileData);
			}

			// If there was any kind of error that prevents the image from being mapped,
			// we load the content as-is and translate virtual addresses using getFileOffsetFromRva
			if(pages.size() == 0)
			{
				fileError = loadImageAsIs(fileData);
			}
		}
		catch(const std::bad_alloc&)
		{
			fileError = ERROR_NOT_ENOUGH_SPACE;
		}
	}

	return fileError;
}

int PeLib::ImageLoader::Load(std::istream & fs, std::streamoff fileOffset, bool loadHeadersOnly)
{
	std::vector<uint8_t> fileData;
	std::streampos fileSize;
	size_t fileSize2;
	int fileError;

	// We need to reset the stream's error state for cases where the file size is too small
	// Sample: retdec-regression-tests\tools\fileinfo\bugs\exotic-pe-files\shutd0wn97.ex
	fs.clear();

	// Get the file size
	fs.seekg(0, std::ios::end);
	fileSize = fs.tellg();

	// Verify overflow of the file offset
	if(fileOffset > fileSize)
		return ERROR_INVALID_FILE;

	// Windows loader refuses to load any file which is larger than 0xFFFFFFFF
	if(((fileSize - fileOffset) >> 32) != 0)
		return setLoaderError(LDR_ERROR_FILE_TOO_BIG);
	fileSize2 = static_cast<size_t>(fileSize - fileOffset);

	// Optimization: Read and verify IMAGE_DOS_HEADER first to see if it *could* be a PE file
	// This prevents reading the entire file (possibly a very large one) just to find out it's not a PE
	if((fileError = verifyDosHeader(fs, fileOffset, fileSize2)) != ERROR_NONE)
		return fileError;

	// Resize the vector so it can hold entire file. Note that this can
	// potentially allocate a very large memory block, so we need to handle that carefully
	try
	{
		fileData.resize(fileSize2);
	}
	catch(const std::bad_alloc&)
	{
		return ERROR_NOT_ENOUGH_SPACE;
	}

	// Read the entire file to memory. Note that under Windows
	// and under low memory condition, the underlying OS call (NtReadFile)
	// can fail on low memory. When that happens, fs.read will read less than
	// required. We need to verify the number of bytes read and return the apropriate error code.
	fs.seekg(fileOffset);
	fs.read(reinterpret_cast<char*>(fileData.data()), fileSize2);
	if(fs.gcount() < (fileSize - fileOffset))
	{
		return ERROR_NOT_ENOUGH_SPACE;
	}

	// Call the Load interface on char buffer
	return Load(fileData, loadHeadersOnly);
}

int PeLib::ImageLoader::Load(const char * fileName, bool loadHeadersOnly)
{
	std::ifstream fs(fileName, std::ifstream::in | std::ifstream::binary);
	if(!fs.is_open())
		return ERROR_OPENING_FILE;

	return Load(fs, loadHeadersOnly);
}

//-----------------------------------------------------------------------------
// Protected functions

void PeLib::ImageLoader::readFromPage(PELIB_FILE_PAGE & page, void * buffer, size_t offsetInPage, size_t bytesInPage)
{
	// Is it a page with actual data?
	if(page.buffer.size())
	{
		memcpy(buffer, page.buffer.data() + offsetInPage, bytesInPage);
	}
	else
	{
		memset(buffer, 0, bytesInPage);
	}
}

void PeLib::ImageLoader::writeToPage(PELIB_FILE_PAGE & page, void * buffer, size_t offsetInPage, size_t bytesInPage)
{
	// Write the data to the page
	page.writeToPage(buffer, offsetInPage, bytesInPage);
}

uint32_t PeLib::ImageLoader::readWriteImage(void * buffer, uint32_t rva, uint32_t bytesToRead, READWRITE ReadWrite)
{
	uint32_t bytesRead = 0;
	uint32_t rvaEnd = rva + bytesToRead;

	// Check the last possible address where we read
	if(rvaEnd > getSizeOfImageAligned())
		rvaEnd = getSizeOfImageAligned();

	// Is the offset within the image?
	if(rva < rvaEnd)
	{
		uint8_t * bufferPtr = static_cast<uint8_t *>(buffer);
		size_t pageIndex = rva / PELIB_PAGE_SIZE;

		// The page index must be in range
		if(pageIndex < pages.size())
		{
			while(rva < rvaEnd)
			{
				PELIB_FILE_PAGE & page = pages[pageIndex++];
				uint32_t offsetInPage = rva & (PELIB_PAGE_SIZE - 1);
				uint32_t bytesInPage = PELIB_PAGE_SIZE - offsetInPage;

				// Perhaps the last page loaded?
				if(bytesInPage > (rvaEnd - rva))
					bytesInPage = (rvaEnd - rva);

				// Perform the read/write operation
				ReadWrite(page, bufferPtr, offsetInPage, bytesInPage);

				// Move pointers
				bufferPtr += bytesInPage;
				bytesRead += bytesInPage;
				rva += bytesInPage;
			}
		}
	}

	// Return the number of bytes that were read
	return bytesRead;
}

uint32_t PeLib::ImageLoader::readWriteImageFile(void * buffer, uint32_t rva, uint32_t bytesToRead, bool bReadOperation)
{
	uint32_t fileOffset = getFileOffsetFromRva(rva);

	// Make sure we won't read/write past the end of the data
	if(fileOffset > rawFileData.size())
		return 0;
	if((fileOffset + bytesToRead) > rawFileData.size())
		bytesToRead = (uint32_t)(rawFileData.size() - fileOffset);

	// Read the data
	if(bytesToRead != 0)
	{
		if(bReadOperation)
			memcpy(buffer, rawFileData.data() + fileOffset, bytesToRead);
		else
			memcpy(rawFileData.data() + fileOffset, buffer, bytesToRead);
	}

	// Return the number of bytes read/written
	return bytesToRead;
}

// 
// There is a specific piece of code in MiParseImageSectionHeaders (see below).
// Note that this is done on the raw image data *BEFORE* the image is mapped to sections
// Causes map difference on this sample: 2e26926a701df980fb56e5905a93bf2d7ba6981ccabc81cf251b3c0ed6afdc26
// * SizeOfHeaders:                0x1000
// * PointerToRawData section[1]:  0x0200 - this actually points to the IMAGE_SECTION_HEADER of section[3]
// Because the PointerToRawData of section[3] is set to zero, the RVA 0xA014 is also set to zero
//
// The code is here:
//
//   //
//   // Fix for Borland linker problem.  The SizeOfRawData can
//   // be a zero, but the PointerToRawData is not zero.
//   // Set it to zero.
//   //
//
//  if(SectionTableEntry->SizeOfRawData == 0) {
//      SectionTableEntry->PointerToRawData = 0;
//  }
//

void PeLib::ImageLoader::processSectionHeader(PELIB_IMAGE_SECTION_HEADER * pSectionHeader)
{
	// Note: Retdec's regression tests don't like it, because they require section headers to have original data
	// Also signature verification stops working if we modify the original data
	if(loaderMode != 0)
	{
		// Fix the section header. Note that this will modify the data in the on-disk version
		// of the image. Any section that will become mapped to this section header
		// will have the corresponding DWORD zeroed, as expected.
		if(pSectionHeader->PointerToRawData != 0 && pSectionHeader->SizeOfRawData == 0)
		{
			pSectionHeader->PointerToRawData = 0;
		}
	}
}

//-----------------------------------------------------------------------------
// Processes relocation entry for IA64 relocation bundle

#define EMARCH_ENC_I17_IMM7B_INST_WORD_X         3
#define EMARCH_ENC_I17_IMM7B_SIZE_X              7
#define EMARCH_ENC_I17_IMM7B_INST_WORD_POS_X     4
#define EMARCH_ENC_I17_IMM7B_VAL_POS_X           0

#define EMARCH_ENC_I17_IMM9D_INST_WORD_X         3
#define EMARCH_ENC_I17_IMM9D_SIZE_X              9
#define EMARCH_ENC_I17_IMM9D_INST_WORD_POS_X     18
#define EMARCH_ENC_I17_IMM9D_VAL_POS_X           7

#define EMARCH_ENC_I17_IMM5C_INST_WORD_X         3
#define EMARCH_ENC_I17_IMM5C_SIZE_X              5
#define EMARCH_ENC_I17_IMM5C_INST_WORD_POS_X     13
#define EMARCH_ENC_I17_IMM5C_VAL_POS_X           16

#define EMARCH_ENC_I17_IC_INST_WORD_X            3
#define EMARCH_ENC_I17_IC_SIZE_X                 1
#define EMARCH_ENC_I17_IC_INST_WORD_POS_X        12
#define EMARCH_ENC_I17_IC_VAL_POS_X              21

#define EMARCH_ENC_I17_IMM41a_INST_WORD_X        1
#define EMARCH_ENC_I17_IMM41a_SIZE_X             10
#define EMARCH_ENC_I17_IMM41a_INST_WORD_POS_X    14
#define EMARCH_ENC_I17_IMM41a_VAL_POS_X          22

#define EMARCH_ENC_I17_IMM41b_INST_WORD_X        1
#define EMARCH_ENC_I17_IMM41b_SIZE_X             8
#define EMARCH_ENC_I17_IMM41b_INST_WORD_POS_X    24
#define EMARCH_ENC_I17_IMM41b_VAL_POS_X          32

#define EMARCH_ENC_I17_IMM41c_INST_WORD_X        2
#define EMARCH_ENC_I17_IMM41c_SIZE_X             23
#define EMARCH_ENC_I17_IMM41c_INST_WORD_POS_X    0
#define EMARCH_ENC_I17_IMM41c_VAL_POS_X          40

#define EMARCH_ENC_I17_SIGN_INST_WORD_X          3
#define EMARCH_ENC_I17_SIGN_SIZE_X               1
#define EMARCH_ENC_I17_SIGN_INST_WORD_POS_X      27
#define EMARCH_ENC_I17_SIGN_VAL_POS_X            63

#define EXT_IMM64(Value, SourceValue32, Size, InstPos, ValPos)   \
    Value |= (((uint64_t)((SourceValue32 >> InstPos) & (((uint64_t)1 << Size) - 1))) << ValPos)

#define INS_IMM64(Value, TargetValue32, Size, InstPos, ValPos)   \
    TargetValue32 = (TargetValue32 & ~(((1 << Size) - 1) << InstPos)) |  \
          ((uint32_t)((((uint64_t)Value >> ValPos) & (((uint64_t)1 << Size) - 1))) << InstPos)

bool PeLib::ImageLoader::processImageRelocation_IA64_IMM64(uint32_t fixupAddress, uint64_t difference)
{
	uint64_t Value64 = 0;
	uint32_t BundleBlock[4];

	// Align the fixup address to bundle address
	fixupAddress = fixupAddress & ~0x0F;

	// Load the 4 32-bit values from the target
	if(readImage(BundleBlock, fixupAddress, sizeof(BundleBlock)) != sizeof(BundleBlock))
		return false;

	//
	// Extract the IMM64 from bundle
	//

	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM7B_INST_WORD_X],
			  EMARCH_ENC_I17_IMM7B_SIZE_X,
			  EMARCH_ENC_I17_IMM7B_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM7B_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM9D_INST_WORD_X],
			  EMARCH_ENC_I17_IMM9D_SIZE_X,
			  EMARCH_ENC_I17_IMM9D_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM9D_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM5C_INST_WORD_X],
			  EMARCH_ENC_I17_IMM5C_SIZE_X,
			  EMARCH_ENC_I17_IMM5C_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM5C_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IC_INST_WORD_X],
			  EMARCH_ENC_I17_IC_SIZE_X,
			  EMARCH_ENC_I17_IC_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IC_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM41a_INST_WORD_X],
			  EMARCH_ENC_I17_IMM41a_SIZE_X,
			  EMARCH_ENC_I17_IMM41a_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM41a_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM41b_INST_WORD_X],
			  EMARCH_ENC_I17_IMM41b_SIZE_X,
			  EMARCH_ENC_I17_IMM41b_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM41b_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM41c_INST_WORD_X],
			  EMARCH_ENC_I17_IMM41c_SIZE_X,
			  EMARCH_ENC_I17_IMM41c_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM41c_VAL_POS_X);
	EXT_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_SIGN_INST_WORD_X],
			  EMARCH_ENC_I17_SIGN_SIZE_X,
			  EMARCH_ENC_I17_SIGN_INST_WORD_POS_X,
			  EMARCH_ENC_I17_SIGN_VAL_POS_X);
	//
	// Update 64-bit address
	//

	Value64 += difference;

	//
	// Insert IMM64 into bundle
	//

	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM7B_INST_WORD_X],
			  EMARCH_ENC_I17_IMM7B_SIZE_X,
			  EMARCH_ENC_I17_IMM7B_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM7B_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM9D_INST_WORD_X],
			  EMARCH_ENC_I17_IMM9D_SIZE_X,
			  EMARCH_ENC_I17_IMM9D_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM9D_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM5C_INST_WORD_X],
			  EMARCH_ENC_I17_IMM5C_SIZE_X,
			  EMARCH_ENC_I17_IMM5C_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM5C_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IC_INST_WORD_X],
			  EMARCH_ENC_I17_IC_SIZE_X,
			  EMARCH_ENC_I17_IC_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IC_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM41a_INST_WORD_X],
			  EMARCH_ENC_I17_IMM41a_SIZE_X,
			  EMARCH_ENC_I17_IMM41a_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM41a_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM41b_INST_WORD_X],
			  EMARCH_ENC_I17_IMM41b_SIZE_X,
			  EMARCH_ENC_I17_IMM41b_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM41b_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_IMM41c_INST_WORD_X],
			  EMARCH_ENC_I17_IMM41c_SIZE_X,
			  EMARCH_ENC_I17_IMM41c_INST_WORD_POS_X,
			  EMARCH_ENC_I17_IMM41c_VAL_POS_X);
	INS_IMM64(Value64, BundleBlock[EMARCH_ENC_I17_SIGN_INST_WORD_X],
			  EMARCH_ENC_I17_SIGN_SIZE_X,
			  EMARCH_ENC_I17_SIGN_INST_WORD_POS_X,
			  EMARCH_ENC_I17_SIGN_VAL_POS_X);

	// Write the bundle block back to the image
	return (writeImage(BundleBlock, fixupAddress, sizeof(BundleBlock)) == sizeof(BundleBlock));
}

bool PeLib::ImageLoader::processImageRelocations(uint64_t oldImageBase, uint64_t newImageBase, uint32_t VirtualAddress, uint32_t Size)
{
	uint64_t difference = (newImageBase - oldImageBase);
	uint8_t * bufferEnd;
	uint8_t * bufferPtr;
	uint8_t * buffer;

	// No not accept anything less than size of relocation block
	// Also refuse to process suspiciously large relocation blocks
	if(Size < sizeof(PELIB_IMAGE_BASE_RELOCATION) || Size > PELIB_SIZE_10MB)
		return false;

	// Allocate and read the relocation block
	bufferPtr = buffer = new uint8_t[Size];
	if(buffer != nullptr)
	{
		// Read the relocations from the file
		bufferEnd = buffer + readImage(buffer, VirtualAddress, Size);

		// Keep going while there is relocation blocks
		while((bufferPtr + sizeof(PELIB_IMAGE_BASE_RELOCATION)) <= bufferEnd)
		{
			PELIB_IMAGE_BASE_RELOCATION * pRelocBlock = (PELIB_IMAGE_BASE_RELOCATION *)(bufferPtr);
			uint16_t * typeAndOffset = (uint16_t * )(pRelocBlock + 1);
			uint32_t numRelocations;

			// Skip relocation blocks that have invalid values
			if(!isValidImageBlock(pRelocBlock->VirtualAddress, pRelocBlock->SizeOfBlock))
				break;

			// Skip relocation blocks which have invalid size in the header
			if(pRelocBlock->SizeOfBlock <= sizeof(PELIB_IMAGE_BASE_RELOCATION))
			{
				bufferPtr += sizeof(PELIB_IMAGE_BASE_RELOCATION);
				continue;
			}

			// Windows loader seems to skip relocation blocks that go into the 0-th page (the header)
			// Sample: e380e6968f1b431e245f811f94cef6a5b6e17fd7c90ef283338fa1959eb3c536
			if(isZeroPage(pRelocBlock->VirtualAddress))
			{
				bufferPtr += pRelocBlock->SizeOfBlock;
				continue;
			}

			// Calculate number of relocation entries. Prevent buffer overflow
			if((bufferPtr + pRelocBlock->SizeOfBlock) > bufferEnd)
				pRelocBlock->SizeOfBlock = bufferEnd - bufferPtr;
			numRelocations = (pRelocBlock->SizeOfBlock - sizeof(PELIB_IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);

			// Parse relocations
			for(uint32_t i = 0; i < numRelocations; i++)
			{
				uint32_t fixupAddress = pRelocBlock->VirtualAddress + (typeAndOffset[i] & 0x0FFF);
				int32_t temp;

				switch(typeAndOffset[i] >> 12)
				{
					case PELIB_IMAGE_REL_BASED_DIR64:         // The base relocation applies the difference to the 64-bit field at offset.
					{
						int64_t fixupValue = 0;

						if(readImage(&fixupValue, fixupAddress, sizeof(fixupValue)) != sizeof(fixupValue))
							break;
						fixupValue += difference;
						writeImage(&fixupValue, fixupAddress, sizeof(fixupValue));
						break;
					}

					case PELIB_IMAGE_REL_BASED_HIGHLOW:       // The base relocation applies all 32 bits of the difference to the 32-bit field at offset.
					{
						int32_t fixupValue = 0;

						if(readImage(&fixupValue, fixupAddress, sizeof(fixupValue)) != sizeof(fixupValue))
							break;
						fixupValue += (int32_t)difference;
						writeImage(&fixupValue, fixupAddress, sizeof(fixupValue));
						break;
					}

					case PELIB_IMAGE_REL_BASED_HIGH:          // The base relocation adds the high 16 bits of the difference to the 16-bit field at offset.
					{
						int16_t fixupValue = 0;

						if(readImage(&fixupValue, fixupAddress, sizeof(fixupValue)) != sizeof(fixupValue))
							break;
						temp = (fixupValue << 16);
						temp += (int32_t)difference;
						fixupValue = (int16_t)(temp >> 16);
						writeImage(&fixupValue, fixupAddress, sizeof(fixupValue));
						break;
					}

					case PELIB_IMAGE_REL_BASED_HIGHADJ:       // The base relocation adds the high 16 bits of the difference to the 16-bit field at offset.
					{
						int16_t fixupValue = 0;

						if(readImage(&fixupValue, fixupAddress, sizeof(fixupValue)) != sizeof(fixupValue))
							break;
						temp = (fixupValue << 16);
						temp += (int32_t)typeAndOffset[++i];
						temp += (int32_t)difference;
						temp += 0x8000;
						fixupValue = (int16_t)(temp >> 16);
						writeImage(&fixupValue, fixupAddress, sizeof(fixupValue));
						break;
					}

					case PELIB_IMAGE_REL_BASED_LOW:           // The base relocation adds the low 16 bits of the difference to the 16-bit field at offset.
					{
						int16_t fixupValue = 0;

						if(readImage(&fixupValue, fixupAddress, sizeof(fixupValue)) != sizeof(fixupValue))
							break;
						fixupValue = (int16_t)((int32_t)fixupValue + difference);
						writeImage(&fixupValue, fixupAddress, sizeof(fixupValue));
						break;
					}

					case PELIB_IMAGE_REL_BASED_MIPS_JMPADDR:  // Relocate a MIPS jump address.
					{
						uint32_t fixupValue = 0;

						if(readImage(&fixupValue, fixupAddress, sizeof(fixupValue)) != sizeof(fixupValue))
							break;
						temp = (fixupValue & 0x3ffffff) << 2;
						temp += (int32_t)difference;
						fixupValue = (fixupValue & ~0x3ffffff) | ((temp >> 2) & 0x3ffffff);
						writeImage(&fixupValue, fixupAddress, sizeof(fixupValue));
						break;
					}

					case PELIB_IMAGE_REL_BASED_IA64_IMM64:
						processImageRelocation_IA64_IMM64(fixupAddress, difference);
						break;

					case PELIB_IMAGE_REL_BASED_ABSOLUTE:      // Absolute - no fixup required.
						break;

					default:
						return false;
				}
			}

			// Move to the next relocation block
			bufferPtr = bufferPtr + pRelocBlock->SizeOfBlock;
		}

		// Free the relocation buffer
		delete [] buffer;
	}

	return true;
}

void PeLib::ImageLoader::writeNewImageBase(uint64_t newImageBase)
{
	uint32_t offset = dosHeader.e_lfanew + sizeof(uint32_t) + sizeof(PELIB_IMAGE_FILE_HEADER);

	// 64-bit images
	if(optionalHeader.Magic == PELIB_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		PELIB_IMAGE_OPTIONAL_HEADER64 header64{};
		uint32_t sizeOfOptionalHeader = getCopySizeOfOptionalHeader<PELIB_IMAGE_OPTIONAL_HEADER64>();

		readImage(&header64, offset, sizeOfOptionalHeader);
		header64.ImageBase = newImageBase;
		writeImage(&header64, offset, sizeOfOptionalHeader);
	}

	// 32-bit images
	if(optionalHeader.Magic == PELIB_IMAGE_NT_OPTIONAL_HDR32_MAGIC)
	{
		PELIB_IMAGE_OPTIONAL_HEADER32 header32{};
		uint32_t sizeOfOptionalHeader = getCopySizeOfOptionalHeader<PELIB_IMAGE_OPTIONAL_HEADER32>();

		readImage(&header32, offset, sizeOfOptionalHeader);
		header32.ImageBase = (uint32_t)newImageBase;
		writeImage(&header32, offset, sizeOfOptionalHeader);
	}
}

int PeLib::ImageLoader::captureDosHeader(std::vector<uint8_t> & fileData)
{
	uint8_t * fileBegin = fileData.data();
	uint8_t * fileEnd = fileBegin + fileData.size();

	// Capture the DOS header
	if((fileBegin + sizeof(PELIB_IMAGE_DOS_HEADER)) >= fileEnd)
		return ERROR_INVALID_FILE;
	memcpy(&dosHeader, fileBegin, sizeof(PELIB_IMAGE_DOS_HEADER));

	// Verify DOS header
	return verifyDosHeader(dosHeader, fileData.size());
}

int PeLib::ImageLoader::captureNtHeaders(std::vector<uint8_t> & fileData)
{
	uint8_t * fileBegin = fileData.data();
	uint8_t * filePtr = fileBegin + dosHeader.e_lfanew;
	uint8_t * fileEnd = fileBegin + fileData.size();
	size_t ntHeaderSize;
	uint16_t optionalHeaderMagic = PELIB_IMAGE_NT_OPTIONAL_HDR32_MAGIC;

	// Windows 7 or newer require that the file size is greater or equal to sizeof(IMAGE_NT_HEADERS)
	// Note that 64-bit kernel requires this to be sizeof(IMAGE_NT_HEADERS64)
	if(ntHeadersSizeCheck)
	{
		uint32_t minFileSize = dosHeader.e_lfanew + sizeof(uint32_t) + sizeof(PELIB_IMAGE_FILE_HEADER) + sizeof(PELIB_IMAGE_OPTIONAL_HEADER32);

		if((filePtr + minFileSize) > fileEnd)
			return setLoaderError(LDR_ERROR_NTHEADER_OUT_OF_FILE);
	}

	// Capture the NT signature
	if((filePtr + sizeof(uint32_t)) >= fileEnd)
	{
		setLoaderError(LDR_ERROR_NTHEADER_OUT_OF_FILE);
		return ERROR_INVALID_FILE;
	}

	// Check the NT signature
	if((ntSignature = *(uint32_t *)(filePtr)) != PELIB_IMAGE_NT_SIGNATURE)
	{
		setLoaderError(LDR_ERROR_NO_NT_SIGNATURE);
		return ERROR_INVALID_FILE;
	}
	filePtr += sizeof(uint32_t);

	// Capture the file header
	if((filePtr + sizeof(PELIB_IMAGE_FILE_HEADER)) < fileEnd)
		memcpy(&fileHeader, filePtr, sizeof(PELIB_IMAGE_FILE_HEADER));
	else
		setLoaderError(LDR_ERROR_NTHEADER_OUT_OF_FILE);

	// 7baebc6d9f2185fafa760c875ab1386f385a0b3fecf2e6ae339abb4d9ac58f3e
	if(fileHeader.Machine == 0 && fileHeader.SizeOfOptionalHeader == 0)
		setLoaderError(LDR_ERROR_FILE_HEADER_INVALID);
	if(!(fileHeader.Characteristics & PELIB_IMAGE_FILE_EXECUTABLE_IMAGE))
		setLoaderError(LDR_ERROR_IMAGE_NON_EXECUTABLE);
	filePtr += sizeof(PELIB_IMAGE_FILE_HEADER);

	// Windows XP: Number of section must be 96
	// Windows 7: Number of section must be 192
	if(fileHeader.NumberOfSections > maxSectionCount)
		setLoaderError(LDR_ERROR_IMAGE_NON_EXECUTABLE);

	// Check the position of the NT header for integer overflow and for file size overflow
	ntHeaderSize = sizeof(uint32_t) + sizeof(PELIB_IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
	if((dosHeader.e_lfanew + ntHeaderSize) < dosHeader.e_lfanew)
		setLoaderError(LDR_ERROR_NTHEADER_OFFSET_OVERFLOW);

	// Capture optional header. Note that we need to parse it
	// according to IMAGE_OPTIONAL_HEADER::Magic
	if((filePtr + sizeof(uint16_t)) < fileEnd)
		optionalHeaderMagic = *(uint16_t *)(filePtr);
	if(optionalHeaderMagic == PELIB_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		captureOptionalHeader64(fileBegin, filePtr, fileEnd);
	else
		captureOptionalHeader32(fileBegin, filePtr, fileEnd);

	// Performed by Windows 10 (nt!MiVerifyImageHeader):
	// Sample: 04d3577d1b6309a0032d4c4c1252c55416a09bb617aebafe512fffbdd4f08f18
	if(appContainerCheck && checkForBadAppContainer())
		setLoaderError(LDR_ERROR_IMAGE_NON_EXECUTABLE);

	// SizeOfHeaders must be nonzero if not a single subsection
	if(optionalHeader.SectionAlignment >= PELIB_PAGE_SIZE && optionalHeader.SizeOfHeaders == 0)
		setLoaderError(LDR_ERROR_SIZE_OF_HEADERS_ZERO);

	// File alignment must not be 0
	if(optionalHeader.FileAlignment == 0)
		setLoaderError(LDR_ERROR_FILE_ALIGNMENT_ZERO);

	// File alignment must be a power of 2
	if(optionalHeader.FileAlignment & (optionalHeader.FileAlignment-1))
		setLoaderError(LDR_ERROR_FILE_ALIGNMENT_NOT_POW2);

	// Section alignment must not be 0
	if(optionalHeader.SectionAlignment == 0)
		setLoaderError(LDR_ERROR_SECTION_ALIGNMENT_ZERO);

	// Section alignment must be a power of 2
	if(optionalHeader.SectionAlignment & (optionalHeader.SectionAlignment - 1))
		setLoaderError(LDR_ERROR_SECTION_ALIGNMENT_NOT_POW2);

	if(optionalHeader.SectionAlignment < optionalHeader.FileAlignment)
		setLoaderError(LDR_ERROR_SECTION_ALIGNMENT_TOO_SMALL);

	// Check for images with "super-section": FileAlignment must be equal to SectionAlignment
	if((optionalHeader.FileAlignment & 511) && (optionalHeader.SectionAlignment != optionalHeader.FileAlignment))
		setLoaderError(LDR_ERROR_SECTION_ALIGNMENT_INVALID);

	// Check for largest image
	if(optionalHeader.SizeOfImage > PELIB_MM_SIZE_OF_LARGEST_IMAGE)
		setLoaderError(LDR_ERROR_SIZE_OF_IMAGE_TOO_BIG);

	// Check for 32-bit images
	if(optionalHeader.Magic == PELIB_IMAGE_NT_OPTIONAL_HDR32_MAGIC && checkForValid32BitMachine() == false)
		setLoaderError(LDR_ERROR_INVALID_MACHINE32);

	// Check for 64-bit images
	if(optionalHeader.Magic == PELIB_IMAGE_NT_OPTIONAL_HDR64_MAGIC && checkForValid64BitMachine() == false)
		setLoaderError(LDR_ERROR_INVALID_MACHINE64);

	// Check the size of image
	if(optionalHeader.SizeOfHeaders > optionalHeader.SizeOfImage)
		setLoaderError(LDR_ERROR_SIZE_OF_HEADERS_INVALID);

	// On 64-bit Windows, size of optional header must be properly aligned to 8-byte boundary
	if(is64BitWindows && (fileHeader.SizeOfOptionalHeader & 0x07))
		setLoaderError(LDR_ERROR_SIZE_OF_OPTHDR_NOT_ALIGNED);

	// Set the size of image
	if(BytesToPages(optionalHeader.SizeOfImage) == 0)
		setLoaderError(LDR_ERROR_SIZE_OF_IMAGE_ZERO);

	// Check for proper alignment of the image base
	if(optionalHeader.ImageBase & (PELIB_SIZE_64KB - 1))
		setLoaderError(LDR_ERROR_IMAGE_BASE_NOT_ALIGNED);

	return ERROR_NONE;
}

int PeLib::ImageLoader::captureSectionName(std::vector<uint8_t> & fileData, std::string & sectionName, const uint8_t * Name)
{
	// If the section name is in format of "/12345", then the section name is actually in the symbol table
	// Sample: 2e9c671b8a0411f2b397544b368c44d7f095eb395779de0ad1ac946914dfa34c
	if(fileHeader.PointerToSymbolTable != 0 && Name[0] == '/')
	{
		// Get the offset of the string table
		uint32_t stringTableOffset = fileHeader.PointerToSymbolTable + fileHeader.NumberOfSymbols * PELIB_IMAGE_SIZEOF_COFF_SYMBOL;
		uint32_t stringTableIndex = 0;

		// Convert the index from string to number
		for (size_t i = 1; i < PELIB_IMAGE_SIZEOF_SHORT_NAME && isdigit(Name[i]); i++)
			stringTableIndex = (stringTableIndex * 10) + (Name[i] - '0');

		// Get the section name
		if(readStringRaw(fileData, sectionName, stringTableOffset + stringTableIndex, PELIB_IMAGE_SIZEOF_MAX_NAME, true, true) != 0)
		    return ERROR_NONE;
	}

	// The section name is directly in the section header.
	// It has fixed length and must not be necessarily terminated with zero.
	// Historically, PELIB copies the name of the section WITHOUT zero chars,
	// even if the zero chars are in the middle. Aka ".text\0\0X" results in ".textX"
	sectionName.clear();
	for(size_t i = 0; i < PELIB_IMAGE_SIZEOF_SHORT_NAME; i++)
	{
		if(Name[i])
		{
			sectionName += Name[i];
		}
	}
	return ERROR_NONE;
}

int PeLib::ImageLoader::captureSectionHeaders(std::vector<uint8_t> & fileData)
{
	uint8_t * fileBegin = fileData.data();
	uint8_t * filePtr;
	uint8_t * fileEnd = fileBegin + fileData.size();
	bool bRawDataBeyondEOF = false;

	// Check whether the sections are within the file
	filePtr = fileBegin + dosHeader.e_lfanew + sizeof(uint32_t) + sizeof(PELIB_IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
	if(filePtr > fileEnd)
		return setLoaderError(LDR_ERROR_SECTION_HEADERS_OUT_OF_IMAGE);

	// Set the counters
	uint32_t NumberOfSectionPTEs = AlignToSize(optionalHeader.SizeOfHeaders, optionalHeader.SectionAlignment) / PELIB_PAGE_SIZE;
	uint64_t NextVirtualAddress = 0;
	uint32_t NumberOfPTEs = BytesToPages(optionalHeader.SizeOfImage);
	uint32_t FileAlignmentMask = optionalHeader.FileAlignment - 1;
	bool SingleSubsection = (optionalHeader.SectionAlignment < PELIB_PAGE_SIZE);

	// Verify the image
	if(!SingleSubsection)
	{
		// Some extra checks done by the loader
		if((optionalHeader.SizeOfHeaders + (optionalHeader.SectionAlignment - 1)) < optionalHeader.SizeOfHeaders)
			setLoaderError(LDR_ERROR_SECTION_HEADERS_OVERFLOW);

		if(NumberOfSectionPTEs > NumberOfPTEs)
			setLoaderError(LDR_ERROR_SIZE_OF_HEADERS_INVALID);

		// Update the virtual address
		NextVirtualAddress += NumberOfSectionPTEs * PELIB_PAGE_SIZE;
		NumberOfPTEs -= NumberOfSectionPTEs;
	}
	else
	{
		NumberOfSectionPTEs = AlignToSize(optionalHeader.SizeOfImage, PELIB_PAGE_SIZE) / PELIB_PAGE_SIZE;
		NumberOfPTEs -= NumberOfSectionPTEs;
	}

	// Read and verify all section headers
	for(uint16_t i = 0; i < fileHeader.NumberOfSections; i++)
	{
		PELIB_SECTION_HEADER sectHdr;

		// Capture one section header
		if((filePtr + sizeof(PELIB_IMAGE_SECTION_HEADER)) > fileEnd)
			break;
		memcpy(&sectHdr, filePtr, sizeof(PELIB_IMAGE_SECTION_HEADER));

		// Fix the section header *in the source data*. We need to do that *after* the section header was loaded
		processSectionHeader((PELIB_IMAGE_SECTION_HEADER *)filePtr);

		// Parse the section headers and check for corruptions
		uint32_t PointerToRawData = (sectHdr.SizeOfRawData != 0) ? sectHdr.PointerToRawData : 0;
		uint32_t EndOfRawData = PointerToRawData + sectHdr.SizeOfRawData;
		uint32_t VirtualSize = (sectHdr.VirtualSize != 0) ? sectHdr.VirtualSize : sectHdr.SizeOfRawData;

		// Overflow check
		if((PointerToRawData + sectHdr.SizeOfRawData) < PointerToRawData)
			setLoaderError(LDR_ERROR_RAW_DATA_OVERFLOW);

		// Verify the image
		if(SingleSubsection)
		{
			// If the image is mapped as single subsection,
			// then the virtual values must match raw values
			if((sectHdr.VirtualAddress != PointerToRawData) || sectHdr.SizeOfRawData < VirtualSize)
				setLoaderError(LDR_ERROR_SECTION_SIZE_MISMATCH);
		}
		else
		{
			// Check the virtual address of the section
			if(NextVirtualAddress != sectHdr.VirtualAddress)
				setLoaderError(LDR_ERROR_INVALID_SECTION_VA);

			// Check the end of the section
			if((NextVirtualAddress + VirtualSize) <= NextVirtualAddress)
				setLoaderError(LDR_ERROR_INVALID_SECTION_VSIZE);

			// Check section size
			if((VirtualSize + (PELIB_PAGE_SIZE - 1)) <= VirtualSize)
				setLoaderError(LDR_ERROR_INVALID_SECTION_VSIZE);

			// Calculate number of PTEs in the section
			NumberOfSectionPTEs = AlignToSize(VirtualSize, optionalHeader.SectionAlignment) / PELIB_PAGE_SIZE;
			if(NumberOfSectionPTEs > NumberOfPTEs)
				setLoaderError(LDR_ERROR_INVALID_SECTION_VSIZE);

			NumberOfPTEs -= NumberOfSectionPTEs;

			// Check end of the raw data for the section
			if(((PointerToRawData + sectHdr.SizeOfRawData + FileAlignmentMask) & ~FileAlignmentMask) < PointerToRawData)
				setLoaderError(LDR_ERROR_INVALID_SECTION_RAWSIZE);

			// On last section, size of raw data must not go after the end of the file
			// Sample: a5957dad4b3a53a5894708c7c1ba91be0668ecbed49e33affee3a18c0737c3a5
			if(i == fileHeader.NumberOfSections - 1 && sectHdr.SizeOfRawData != 0)
			{
				if((sectHdr.PointerToRawData + sectHdr.SizeOfRawData) > fileData.size())
					setLoaderError(LDR_ERROR_FILE_IS_CUT);
			}

			NextVirtualAddress += NumberOfSectionPTEs * PELIB_PAGE_SIZE;
		}

		// Check for raw data beyond end-of-file
		// Note that Windows loader doesn't check this on files that are mapped as single section.
		// We will do that nonetheless, because we want to know that a file is cut.
		if(PointerToRawData != 0 && (fileBegin + EndOfRawData) > fileEnd)
			bRawDataBeyondEOF = true;

		// Resolve the section name
		captureSectionName(fileData, sectHdr.sectionName, sectHdr.Name);

		// Insert the header to the list
		sections.push_back(sectHdr);
		filePtr += sizeof(PELIB_IMAGE_SECTION_HEADER);
	}

	// Verify the image size. Note that this check is no longer performed by Windows 10
	if(sizeofImageMustMatch)
	{
		uint32_t ThresholdNumberOfPTEs = (SingleSubsection == false) ? (optionalHeader.SectionAlignment / PELIB_PAGE_SIZE) : 1;
		if(NumberOfPTEs >= ThresholdNumberOfPTEs)
		{
			setLoaderError(LDR_ERROR_INVALID_SIZE_OF_IMAGE);
		}
	}

	// Did we detect a trimmed file?
	if(bRawDataBeyondEOF)
	{
		// Track the state of loadability of the cut file. Some files can still be loadable.
		// Example: bd149478739e660b032e4454057ce8d3e18dfbb6d1677c6ecdcc3aa59b36c8d9
		bool bCutButLoadable = false;

		// Special exception: Even if cut, the file is still loadable
		// if the last section is in the file range. This is because
		// the PE loader in Windows only cares about whether the last section is in the file range
		if(SingleSubsection == false)
		{
			if(!sections.empty())
			{
				PELIB_IMAGE_SECTION_HEADER & lastSection = sections.back();
				uint32_t PointerToRawData = (lastSection.SizeOfRawData != 0) ? lastSection.PointerToRawData : 0;
				uint32_t EndOfRawData = PointerToRawData + lastSection.SizeOfRawData;

				if((lastSection.SizeOfRawData == 0) || (fileBegin + EndOfRawData) <= fileEnd)
				{
					setLoaderError(LDR_ERROR_FILE_IS_CUT_LOADABLE);
					bCutButLoadable = true;
				}
			}
		}
		else
		{
			setLoaderError(LDR_ERROR_FILE_IS_CUT_LOADABLE);
			bCutButLoadable = true;
		}

		// If the file is not loadable, set the "file is cut" error
		if(bCutButLoadable == false)
		{
			setLoaderError(LDR_ERROR_FILE_IS_CUT);
		}
	}

	return ERROR_NONE;
}

int PeLib::ImageLoader::captureImageSections(std::vector<uint8_t> & fileData)
{
	uint32_t virtualAddress = 0;
	uint32_t sizeOfHeaders = optionalHeader.SizeOfHeaders;
	uint32_t sizeOfImage = optionalHeader.SizeOfImage;

	// Section-based mapping / file-based mapping
	if(optionalHeader.SectionAlignment >= PELIB_PAGE_SIZE)
	{
		// Reserve the image size, aligned up to the page size
		sizeOfImage = AlignToSize(sizeOfImage, PELIB_PAGE_SIZE);
		pages.resize(sizeOfImage / PELIB_PAGE_SIZE);

		// Note: Under Windows XP, the loader maps the entire page of the image header
		// if the condition in checkForSectionTablesWithinHeader() turns out to be true.
		// Windows 7+ uses correct size check.
		// Sample: 1669f0220f1f74523390fe5b61ea09d6e2e4e798ab294c93d0a20900a3c5a52a
		// (Any sample with 4 sections and IMAGE_DOS_HEADER::e_lfanew >= 0x724 will do)
		if(headerSizeCheck && checkForSectionTablesWithinHeader(dosHeader.e_lfanew))
			sizeOfHeaders = AlignToSize(sizeOfHeaders, optionalHeader.SectionAlignment);

		// Capture the file header
		virtualAddress = captureImageSection(fileData, virtualAddress, sizeOfHeaders, 0, sizeOfHeaders, PELIB_IMAGE_SCN_MEM_READ, true);
		if(virtualAddress == 0)
			return ERROR_INVALID_FILE;

		// Capture each section
		if(sections.size() != 0)
		{
			for(auto & sectionHeader : sections)
			{
				// Capture all pages from the section
				if(captureImageSection(fileData, sectionHeader.VirtualAddress,
												 sectionHeader.VirtualSize,
												 sectionHeader.PointerToRawData,
												 sectionHeader.SizeOfRawData,
												 sectionHeader.Characteristics) == 0)
				{
					setLoaderError(LDR_ERROR_INVALID_SECTION_VA);
					break;
				}
			}
		}
		else
		{
			// If the file has no sections, we need to check the SizeOfImage against
			// the virtual address. They must match, otherwise Windows will not load the file.
			// Sample: cdf2a3ff23ec8a0814e285d94c4f081202ea6fe69661ff9940dcafc28e5fc626
			if(virtualAddress > optionalHeader.SizeOfImage || (optionalHeader.SizeOfImage - virtualAddress) > optionalHeader.SectionAlignment)
			{
				setLoaderError(LDR_ERROR_INVALID_SIZE_OF_IMAGE);
			}
		}
	}
	else
	{
		// 64-bit Windows always align single-section images to page size.
		// 32-bit Windows: 
		// * Windows XP: sector size
		// * Windows 7 : sector size (network files) or no align (local files)
		// * Windows 10: no align
		// If the image is smaller than one page, it is aligned to one page
		sizeOfImage = AlignToSize(sizeOfImage, ssiImageAlignment32);
		if(is64BitWindows)
			sizeOfImage = AlignToSize(sizeOfImage, PELIB_PAGE_SIZE);
		if(sizeOfImage < PELIB_PAGE_SIZE)
			sizeOfImage = PELIB_PAGE_SIZE;
		pages.resize((sizeOfImage + PELIB_PAGE_SIZE - 1) / PELIB_PAGE_SIZE);

		// Capture the file as-is
		virtualAddress = captureImageSection(fileData, 0, sizeOfImage, 0, sizeOfImage, PELIB_IMAGE_SCN_MEM_WRITE | PELIB_IMAGE_SCN_MEM_READ | PELIB_IMAGE_SCN_MEM_EXECUTE, true);
		if(virtualAddress == 0)
			return ERROR_INVALID_FILE;
	}

	return ERROR_NONE;
}

int PeLib::ImageLoader::verifyDosHeader(PELIB_IMAGE_DOS_HEADER & hdr, size_t fileSize)
{
	if(hdr.e_magic != PELIB_IMAGE_DOS_SIGNATURE)
		return ERROR_INVALID_FILE;
	if(hdr.e_lfanew & 3)
		return setLoaderError(LDR_ERROR_E_LFANEW_UNALIGNED);
	if(hdr.e_lfanew > fileSize)
		return setLoaderError(LDR_ERROR_E_LFANEW_OUT_OF_FILE);

	return ERROR_NONE;
}

int PeLib::ImageLoader::verifyDosHeader(std::istream & fs, std::streamoff fileOffset, size_t fileSize)
{
	PELIB_IMAGE_DOS_HEADER tempDosHeader;
	int fileError;

	// The file size must be at least size of DOS header
	if((fileOffset + sizeof(PELIB_IMAGE_DOS_HEADER)) >= fileSize)
		return ERROR_INVALID_FILE;
	fs.seekg(fileOffset);

	// Read the DOS header
	if(fs.read(reinterpret_cast<char*>(&tempDosHeader), sizeof(PELIB_IMAGE_DOS_HEADER)).bad())
		return ERROR_INVALID_FILE;

	// Verify the DOS header
	if((fileError = verifyDosHeader(tempDosHeader, fileSize)) != ERROR_NONE)
		return fileError;

	// If the DOS header points out of the file, it's a wrong file too
	return (ldrError == LDR_ERROR_E_LFANEW_OUT_OF_FILE) ? ERROR_INVALID_FILE : ERROR_NONE;
}

int PeLib::ImageLoader::loadImageAsIs(std::vector<uint8_t> & fileData)
{
	rawFileData = fileData;
	return ERROR_NONE;
}

int PeLib::ImageLoader::captureOptionalHeader64(uint8_t * fileBegin, uint8_t * filePtr, uint8_t * fileEnd)
{
	PELIB_IMAGE_OPTIONAL_HEADER64 optionalHeader64{};
	uint8_t * dataDirectoryPtr;
	uint32_t sizeOfOptionalHeader = sizeof(PELIB_IMAGE_OPTIONAL_HEADER64);
	uint32_t numberOfRvaAndSizes;

	// Capture optional header. Note that IMAGE_FILE_HEADER::SizeOfOptionalHeader
	// is not taken into account by the Windows loader - it simply assumes that the entire optional header is present
	if((filePtr + sizeOfOptionalHeader) > fileEnd)
		sizeOfOptionalHeader = (uint32_t)(fileEnd - filePtr);
	memcpy(&optionalHeader64, filePtr, sizeOfOptionalHeader);

	// Verify whether it's 64-bit optional header
	if(optionalHeader64.Magic != PELIB_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		return setLoaderError(LDR_ERROR_NO_OPTHDR_MAGIC);

	// Convert 32-bit optional header to common optional header
	optionalHeader.Magic                       = optionalHeader64.Magic;
	optionalHeader.MajorLinkerVersion          = optionalHeader64.MajorLinkerVersion;
	optionalHeader.MinorLinkerVersion          = optionalHeader64.MinorLinkerVersion;
	optionalHeader.SizeOfCode                  = optionalHeader64.SizeOfCode;
	optionalHeader.SizeOfInitializedData       = optionalHeader64.SizeOfInitializedData;
	optionalHeader.SizeOfUninitializedData     = optionalHeader64.SizeOfUninitializedData;
	optionalHeader.AddressOfEntryPoint         = optionalHeader64.AddressOfEntryPoint;
	optionalHeader.BaseOfCode                  = optionalHeader64.BaseOfCode;
	optionalHeader.ImageBase                   = optionalHeader64.ImageBase;
	optionalHeader.SectionAlignment            = optionalHeader64.SectionAlignment;
	optionalHeader.FileAlignment               = optionalHeader64.FileAlignment;
	optionalHeader.MajorOperatingSystemVersion = optionalHeader64.MajorOperatingSystemVersion;
	optionalHeader.MinorOperatingSystemVersion = optionalHeader64.MinorOperatingSystemVersion;
	optionalHeader.MajorImageVersion           = optionalHeader64.MajorImageVersion;
	optionalHeader.MinorImageVersion           = optionalHeader64.MinorImageVersion;
	optionalHeader.MajorSubsystemVersion       = optionalHeader64.MajorSubsystemVersion;
	optionalHeader.MinorSubsystemVersion       = optionalHeader64.MinorSubsystemVersion;
	optionalHeader.Win32VersionValue           = optionalHeader64.Win32VersionValue;
	optionalHeader.SizeOfImage                 = optionalHeader64.SizeOfImage;
	optionalHeader.SizeOfHeaders               = optionalHeader64.SizeOfHeaders;
	optionalHeader.CheckSum                    = optionalHeader64.CheckSum;
	optionalHeader.Subsystem                   = optionalHeader64.Subsystem;
	optionalHeader.DllCharacteristics          = optionalHeader64.DllCharacteristics;
	optionalHeader.SizeOfStackReserve          = optionalHeader64.SizeOfStackReserve;
	optionalHeader.SizeOfStackCommit           = optionalHeader64.SizeOfStackCommit;
	optionalHeader.SizeOfHeapReserve           = optionalHeader64.SizeOfHeapReserve;
	optionalHeader.SizeOfHeapCommit            = optionalHeader64.SizeOfHeapCommit;
	optionalHeader.LoaderFlags                 = optionalHeader64.LoaderFlags;
	optionalHeader.NumberOfRvaAndSizes         = optionalHeader64.NumberOfRvaAndSizes;

	// Copy data directories
	if((numberOfRvaAndSizes = optionalHeader64.NumberOfRvaAndSizes) > PELIB_IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
		numberOfRvaAndSizes = PELIB_IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
	memcpy(optionalHeader.DataDirectory, optionalHeader64.DataDirectory, sizeof(PELIB_IMAGE_DATA_DIRECTORY) * numberOfRvaAndSizes);

	// Cut the real number of data directory entries by the file size
	dataDirectoryPtr = filePtr + offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, DataDirectory);
	if(dataDirectoryPtr < fileEnd)
	{
		if((dataDirectoryPtr + numberOfRvaAndSizes * sizeof(PELIB_IMAGE_DATA_DIRECTORY)) > fileEnd)
			numberOfRvaAndSizes = (fileEnd - dataDirectoryPtr + sizeof(PELIB_IMAGE_DATA_DIRECTORY) - 1) / sizeof(PELIB_IMAGE_DATA_DIRECTORY);
	}
	realNumberOfRvaAndSizes = numberOfRvaAndSizes;

	// Remember the offset of the checksum field
	checkSumFileOffset = (filePtr - fileBegin) + offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, CheckSum);
	securityDirFileOffset = (filePtr - fileBegin) + offsetof(PELIB_IMAGE_OPTIONAL_HEADER64, DataDirectory) + (sizeof(PELIB_IMAGE_DATA_DIRECTORY) * PELIB_IMAGE_DIRECTORY_ENTRY_SECURITY);
	return ERROR_NONE;
}

int PeLib::ImageLoader::captureOptionalHeader32(uint8_t * fileBegin, uint8_t * filePtr, uint8_t * fileEnd)
{
	PELIB_IMAGE_OPTIONAL_HEADER32 optionalHeader32{};
	uint8_t * dataDirectoryPtr;
	uint32_t sizeOfOptionalHeader = sizeof(PELIB_IMAGE_OPTIONAL_HEADER32);
	uint32_t numberOfRvaAndSizes;

	// Capture optional header. Note that IMAGE_FILE_HEADER::SizeOfOptionalHeader
	// is not taken into account by the Windows loader - it simply assumes that the entire optional header is present
	if((filePtr + sizeOfOptionalHeader) > fileEnd)
		sizeOfOptionalHeader = (uint32_t)(fileEnd - filePtr);
	memcpy(&optionalHeader32, filePtr, sizeOfOptionalHeader);

	// Note: Do not fail if there's no magic value for 32-bit optional header
	if(optionalHeader32.Magic != PELIB_IMAGE_NT_OPTIONAL_HDR32_MAGIC)
		setLoaderError(LDR_ERROR_NO_OPTHDR_MAGIC);

	// Convert 32-bit optional header to common optional header
	optionalHeader.Magic                       = optionalHeader32.Magic;
	optionalHeader.MajorLinkerVersion          = optionalHeader32.MajorLinkerVersion;
	optionalHeader.MinorLinkerVersion          = optionalHeader32.MinorLinkerVersion;
	optionalHeader.SizeOfCode                  = optionalHeader32.SizeOfCode;
	optionalHeader.SizeOfInitializedData       = optionalHeader32.SizeOfInitializedData;
	optionalHeader.SizeOfUninitializedData     = optionalHeader32.SizeOfUninitializedData;
	optionalHeader.AddressOfEntryPoint         = optionalHeader32.AddressOfEntryPoint;
	optionalHeader.BaseOfCode                  = optionalHeader32.BaseOfCode;
	optionalHeader.BaseOfData                  = optionalHeader32.BaseOfData;
	optionalHeader.ImageBase                   = optionalHeader32.ImageBase;
	optionalHeader.SectionAlignment            = optionalHeader32.SectionAlignment;
	optionalHeader.FileAlignment               = optionalHeader32.FileAlignment;
	optionalHeader.MajorOperatingSystemVersion = optionalHeader32.MajorOperatingSystemVersion;
	optionalHeader.MinorOperatingSystemVersion = optionalHeader32.MinorOperatingSystemVersion;
	optionalHeader.MajorImageVersion           = optionalHeader32.MajorImageVersion;
	optionalHeader.MinorImageVersion           = optionalHeader32.MinorImageVersion;
	optionalHeader.MajorSubsystemVersion       = optionalHeader32.MajorSubsystemVersion;
	optionalHeader.MinorSubsystemVersion       = optionalHeader32.MinorSubsystemVersion;
	optionalHeader.Win32VersionValue           = optionalHeader32.Win32VersionValue;
	optionalHeader.SizeOfImage                 = optionalHeader32.SizeOfImage;
	optionalHeader.SizeOfHeaders               = optionalHeader32.SizeOfHeaders;
	optionalHeader.CheckSum                    = optionalHeader32.CheckSum;
	optionalHeader.Subsystem                   = optionalHeader32.Subsystem;
	optionalHeader.DllCharacteristics          = optionalHeader32.DllCharacteristics;
	optionalHeader.SizeOfStackReserve          = optionalHeader32.SizeOfStackReserve;
	optionalHeader.SizeOfStackCommit           = optionalHeader32.SizeOfStackCommit;
	optionalHeader.SizeOfHeapReserve           = optionalHeader32.SizeOfHeapReserve;
	optionalHeader.SizeOfHeapCommit            = optionalHeader32.SizeOfHeapCommit;
	optionalHeader.LoaderFlags                 = optionalHeader32.LoaderFlags;
	optionalHeader.NumberOfRvaAndSizes         = optionalHeader32.NumberOfRvaAndSizes;

	// Copy data directories
	if((numberOfRvaAndSizes = optionalHeader32.NumberOfRvaAndSizes) > PELIB_IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
		numberOfRvaAndSizes = PELIB_IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
	memcpy(optionalHeader.DataDirectory, optionalHeader32.DataDirectory, sizeof(PELIB_IMAGE_DATA_DIRECTORY) * numberOfRvaAndSizes);

	// Cut the real number of data directory entries by the file size
	dataDirectoryPtr = filePtr + offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, DataDirectory);
	if(dataDirectoryPtr < fileEnd)
	{
		if((dataDirectoryPtr + numberOfRvaAndSizes * sizeof(PELIB_IMAGE_DATA_DIRECTORY)) > fileEnd)
			numberOfRvaAndSizes = (fileEnd - dataDirectoryPtr + sizeof(PELIB_IMAGE_DATA_DIRECTORY) - 1) / sizeof(PELIB_IMAGE_DATA_DIRECTORY);
	}
	realNumberOfRvaAndSizes = numberOfRvaAndSizes;

	// Remember the offset of the checksum field
	checkSumFileOffset = (filePtr - fileBegin) + offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, CheckSum);
	securityDirFileOffset = (filePtr - fileBegin) + offsetof(PELIB_IMAGE_OPTIONAL_HEADER32, DataDirectory) + (sizeof(PELIB_IMAGE_DATA_DIRECTORY) * PELIB_IMAGE_DIRECTORY_ENTRY_SECURITY);
	return ERROR_NONE;
}

uint32_t PeLib::ImageLoader::captureImageSection(
	std::vector<uint8_t> & fileData,
	uint32_t virtualAddress,
	uint32_t virtualSize,
	uint32_t pointerToRawData,
	uint32_t sizeOfRawData,
	uint32_t characteristics,
	bool isImageHeader)
{
	uint8_t * fileBegin = fileData.data();
	uint8_t * rawDataPtr;
	uint8_t * rawDataEnd;
	uint8_t * fileEnd = fileBegin + fileData.size();
	uint32_t sizeOfInitializedPages;            // The part of section with initialized pages
	uint32_t sizeOfValidPages;                  // The part of section with valid pages
	uint32_t sizeOfSection;                     // Total virtual size of the section
	uint32_t pageOffset = 0;
	size_t pageIndex;

	// If the virtual size of a section is zero, take the size of raw data
	virtualSize = (virtualSize == 0) ? sizeOfRawData : virtualSize;

	// Virtual size is aligned to PAGE_SIZE (not SectionAlignment!)
	// If SectionAlignment > PAGE_SIZE, header and sections are padded with invalid pages (PAGE_NOACCESS)
	// Sample: f73e66052c8b0a49d56ccadcecdf497c015b5ec6f6724e056f35b57b59afaf59
	virtualSize = AlignToSize(virtualSize, PELIB_PAGE_SIZE);

	// If SizeOfRawData is greater than VirtualSize, cut it to virtual size
	// Note that up to the aligned virtual size, the data are in the section
	if(sizeOfRawData > virtualSize)
		sizeOfRawData = virtualSize;

	// If SectionAlignment is greater than page size, then there are going to be
	// gaps of inaccessible memory after the end of raw data
	// Example: b811f2c047a3e828517c234bd4aa4883e1ec591d88fad21289ae68a6915a6665
	// * has 0x1000 bytes of inaccessible memory at ImageBase+0x1000 (1 page after section header)
	sizeOfInitializedPages = AlignToSize(sizeOfRawData, PELIB_PAGE_SIZE);
	sizeOfValidPages = AlignToSize(virtualSize, PELIB_PAGE_SIZE);
	sizeOfSection = AlignToSize(virtualSize, optionalHeader.SectionAlignment);

	// Get the range of the file containing valid data (aka nonzeros)
	// Pointer to raw data is aligned down to the sector size
	// due to the Windows Loader logic that sets sector offset in the page table entries
	rawDataPtr = fileBegin + (pointerToRawData & ~(PELIB_SECTOR_SIZE - 1));
	rawDataEnd = rawDataPtr + sizeOfRawData;

	// End of raw data is aligned to the file alignment. This does not apply to image header
	// Sample: ab0a9c4a8beee49a13cbf6c684b58f9604d673c9d5522a73ec5dffda909695a1
	// SizeOfHeaders = 0x400, FileAlignment = 0x1000. Only 0x400 bytes is copied to the image
	if(isImageHeader == false)
		rawDataEnd = fileBegin + AlignToSize(pointerToRawData + sizeOfRawData, optionalHeader.FileAlignment);

	// Virtual address must begin exactly at the end of previous VA
	pageIndex = virtualAddress / PELIB_PAGE_SIZE;

	// Some combination of flags in IMAGE_SECTION_HEADER::Characteristics give PAGE_NOACCESS
	// If the image is mapped with SEC_IMAGE_NO_EXECUTE (Windows 10),
	// some of the NOACCESS sections turn into READONLY sections.
	if(getImageProtection(characteristics) != PELIB_PAGE_NOACCESS)
	{
		// If the pointerToRawData is less than SECTOR_SIZE, it will contain file header in it.
		// However, if the pointerToRawData contains 0, then the
		if(pointerToRawData || isImageHeader)
		{
			// Fill all pages that contain data
			while(pageOffset < sizeOfInitializedPages)
			{
				PELIB_FILE_PAGE & filePage = pages[pageIndex++];

				// Only if we didn't get out of the file
				if(rawDataPtr < fileEnd)
				{
					size_t bytesToCopy = PELIB_PAGE_SIZE;

					// Check range validity
					if((rawDataPtr + bytesToCopy) > fileEnd)
						bytesToCopy = (fileEnd - rawDataPtr);
					if((rawDataPtr + bytesToCopy) > rawDataEnd)
						bytesToCopy = (rawDataEnd - rawDataPtr);

					// Initialize the page with valid data
					filePage.setValidPage(rawDataPtr, bytesToCopy);
				}
				else
				{
					filePage.setZeroPage();
				}

				// Move pointers
				rawDataPtr += PELIB_PAGE_SIZE;
				pageOffset += PELIB_PAGE_SIZE;
			}
		}

		// Fill all pages that contain zeroed pages
		while(pageOffset < sizeOfValidPages)
		{
			PELIB_FILE_PAGE & filePage = pages[pageIndex++];

			filePage.setZeroPage();
			pageOffset += PELIB_PAGE_SIZE;
		}
	}

	// Leave all other pages filled with zeros
	return virtualAddress + sizeOfSection;
}

bool PeLib::ImageLoader::isGoodPagePointer(PFN_VERIFY_ADDRESS PfnVerifyAddress, void * pagePtr)
{
	// If the caller didn't supply a verification procedure, use default one
	// The verification procedure can possibly be system-specific, like IsBadReadPtr on Windows
	if(PfnVerifyAddress == nullptr)
	{
		// In order to work in Windows, it must be built with /EHa
		// (Enable C++ Exceptions: Yes with SEH Exceptions (/EHa))
		try
		{
			uint8_t dummyBuffer[0x10] = {0};
			memcmp(pagePtr, dummyBuffer, sizeof(dummyBuffer));
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
	else
	{
		return PfnVerifyAddress(pagePtr, PELIB_PAGE_SIZE);
	}
}

bool PeLib::ImageLoader::isGoodMappedPage(uint32_t rva)
{
	uint32_t pageIndex = (rva / PELIB_PAGE_SIZE);

	return (pageIndex < pages.size()) ? !pages[pageIndex].isInvalidPage : false;
}

bool PeLib::ImageLoader::isZeroPage(uint32_t rva)
{
	uint32_t pageIndex = (rva / PELIB_PAGE_SIZE);

	return (pageIndex < pages.size()) ? pages[pageIndex].isZeroPage : false;
}

bool PeLib::ImageLoader::isSectionHeaderPointerToRawData(uint32_t fileOffset)
{
	uint32_t fileOffsetToSectionHeader = dosHeader.e_lfanew + sizeof(uint32_t) + sizeof(PELIB_IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
	uint32_t fileOffsetOfPointerToRawData;

	// If there is at least one section
	for(size_t i = 0; i < sections.size(); i++, fileOffsetToSectionHeader += sizeof(PELIB_IMAGE_SECTION_HEADER))
	{
		// Get the reference to the section header
		PELIB_IMAGE_SECTION_HEADER & sectHdr = sections[i];

		// Must be a section with SizeOfRawData = 0
		if(sectHdr.SizeOfRawData == 0)
		{
			// Calculate the RVA of the PointerToRawData variable in the last section
			fileOffsetOfPointerToRawData = fileOffsetToSectionHeader + 0x14;  // FIELD_OFFSET(PELIB_IMAGE_SECTION_HEADER, PointerToRawData)

			if(fileOffsetOfPointerToRawData <= fileOffset && fileOffset < fileOffsetOfPointerToRawData + sizeof(uint32_t))
				return true;
		}
	}

	return false;
}

// MiIsLegacyImageArchitecture from Windows 10
bool PeLib::ImageLoader::isLegacyImageArchitecture(uint16_t Machine)
{
	if(Machine == PELIB_IMAGE_FILE_MACHINE_I386)
		return true;
	if(Machine == PELIB_IMAGE_FILE_MACHINE_AMD64)
		return true;
	return false;
}

bool PeLib::ImageLoader::checkForValid64BitMachine()
{
	// Since Windows 10, image loader will load 64-bit ARM images
	if(loadArmImages && fileHeader.Machine == PELIB_IMAGE_FILE_MACHINE_ARM64)
		return true;
	return (fileHeader.Machine == PELIB_IMAGE_FILE_MACHINE_AMD64 || fileHeader.Machine == PELIB_IMAGE_FILE_MACHINE_IA64);
}

bool PeLib::ImageLoader::checkForValid32BitMachine()
{
	// Since Windows 10, image loader will load 32-bit ARM images
	if(loadArmImages && fileHeader.Machine == PELIB_IMAGE_FILE_MACHINE_ARMNT)
		return true;
	return (fileHeader.Machine == PELIB_IMAGE_FILE_MACHINE_I386);
}

// Windows 10: For IMAGE_FILE_MACHINE_I386 and IMAGE_FILE_MACHINE_AMD64,
// if(Characteristics & IMAGE_FILE_RELOCS_STRIPPED) and (DllCharacteristics & IMAGE_DLLCHARACTERISTICS_APPCONTAINER),
// MiVerifyImageHeader returns STATUS_INVALID_IMAGE_FORMAT.
bool PeLib::ImageLoader::checkForBadAppContainer()
{
	if(isLegacyImageArchitecture(fileHeader.Machine))
	{
		if(optionalHeader.DllCharacteristics & PELIB_IMAGE_DLLCHARACTERISTICS_APPCONTAINER)
		{
			if(fileHeader.Characteristics & PELIB_IMAGE_FILE_RELOCS_STRIPPED)
			{
				return true;
			}
		}
	}

	return false;
}

// Weirdly incorrect check performed by Windows XP's MiCreateImageFileMap.
bool PeLib::ImageLoader::checkForSectionTablesWithinHeader(uint32_t e_lfanew)
{
	uint32_t OffsetToSectionTable = sizeof(uint32_t) + sizeof(PELIB_IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
	uint32_t NumberOfSubsections = fileHeader.NumberOfSections;
	uint32_t NtHeaderSize = PELIB_PAGE_SIZE - e_lfanew;

	// If this condition is true, then the image header contains data up fo SizeofHeaders
	// If not, the image header contains the entire page.
	if((e_lfanew + OffsetToSectionTable + (NumberOfSubsections + 1) * sizeof(PELIB_IMAGE_SECTION_HEADER)) <= NtHeaderSize)
		return false;

	return true;
}

// Returns true if the image is OK and can be mapped by NtCreateSection(SEC_IMAGE).
// This does NOT mean that the image is executable by CreateProcess - more checks are done,
// like resource integrity or relocation table correctness.
bool PeLib::ImageLoader::isImageLoadable() const
{
	return (ldrError == LDR_ERROR_NONE || ldrError == LDR_ERROR_FILE_IS_CUT_LOADABLE);
}

bool PeLib::ImageLoader::isImageMappedOk() const
{
	// If there was loader error, we didn't map the image
	if(!isImageLoadable())
		return false;
	if(pages.size() == 0)
		return false;
	return true;
}

bool PeLib::ImageLoader::isValidImageBlock(uint32_t Rva, uint32_t Size) const
{
	if(Rva >= optionalHeader.SizeOfImage || Size >= optionalHeader.SizeOfImage)
		return false;
	if((Rva + Size) < Rva)
		return false;
	if((Rva + Size) > optionalHeader.SizeOfImage)
		return false;
	return true;
}

//-----------------------------------------------------------------------------
// Testing functions

size_t PeLib::ImageLoader::getMismatchOffset(void * buffer1, void * buffer2, uint32_t rva, size_t length)
{
	uint8_t * byteBuffer1 = reinterpret_cast<uint8_t *>(buffer1);
	uint8_t * byteBuffer2 = reinterpret_cast<uint8_t *>(buffer2);
	uint32_t fileOffset = getFileOffsetFromRva(rva);

	for(size_t i = 0; i < length; i++)
	{
		if(byteBuffer1[i] != byteBuffer2[i])
		{
			// Windows loader puts 0 in IMAGE_SECTION_HEADER::PointerToRawData if IMAGE_SECTION_HEADER::SizeOfRawData is also zero.
			// However, this is somewhat random - depends on current memory condition, often dissappears
			// when the sample is copied to another location.
			if(isSectionHeaderPointerToRawData(fileOffset + i))
				continue;

			//for(int j = i & 0xFFFFFFF0; j < 0xD00; j++)
			//	printf("byteBuffer1[j]: %02x, byteBuffer2[j]: %02x\n", byteBuffer1[j], byteBuffer2[j]);
			return i;
		}
	}

	return (size_t)(-1);
}

void PeLib::ImageLoader::compareWithWindowsMappedImage(PELIB_IMAGE_COMPARE & ImageCompare, void * imageDataPtr, uint32_t imageSize)
{
	uint8_t * winImageData = reinterpret_cast<uint8_t *>(imageDataPtr);
	uint8_t * winImageEnd = winImageData + imageSize;
	uint8_t singlePage[PELIB_PAGE_SIZE];
	size_t mismatchOffset;
	size_t rva = 0;

	// Are both loaded?
	if(winImageData != nullptr && isImageMappedOk())
	{
		// Check whether the image size is the same
		if(imageSize != getSizeOfImageAligned())
		{
			ImageCompare.compareResult = ImagesDifferentSize;
			ImageCompare.differenceOffset = 0;
			return;
		}

		// Compare images page-by-page
		while(winImageData < winImageEnd)
		{
			// If the windows page is inaccessible, our page must be inaccessible as well
			bool isGoodPageWin = isGoodPagePointer(ImageCompare.PfnVerifyAddress, winImageData);
			bool isGoodPageMy  = isGoodMappedPage(rva);

			// If we have a compare callback, call it
			if(ImageCompare.PfnCompareCallback != nullptr)
			{
				ImageCompare.PfnCompareCallback(&ImageCompare, rva, imageSize);
			}

			// Both are accessible -> Compare the page
			if(isGoodPageWin && isGoodPageMy)
			{
				// Read the image page
				readImage(singlePage, rva, sizeof(singlePage));

				// Windows: Under low memory condition and heavy load, there may be STATUS_IN_PAGE_ERROR
				// exception thrown when touching the mapped image. For that reason,
				// this function must be framed by __try/__except in caller
				if(memcmp(winImageData, singlePage, PELIB_PAGE_SIZE))
				{
					mismatchOffset = getMismatchOffset(winImageData, singlePage, rva, PELIB_PAGE_SIZE);
					if(mismatchOffset != (size_t)(-1))
					{
						ImageCompare.compareResult = ImagesDifferentPageValue;
						ImageCompare.differenceOffset = rva + mismatchOffset;
						return;
					}
				}
			}
			else
			{
				// Accessible vs inacessible?
				if(isGoodPageWin != isGoodPageMy)
				{
					ImageCompare.compareResult = ImagesDifferentPageAccess;
					ImageCompare.differenceOffset = rva;
					return;
				}
			}

			// Move pointers
			winImageData += PELIB_PAGE_SIZE;
			rva += PELIB_PAGE_SIZE;
		}
	}

	// Check whether both we and Windows mapped the image OK
	if(isImageMappedOk())
	{
		// Windows didn't map the image
		if(winImageData == nullptr)
		{
			ImageCompare.compareResult = ImagesWindowsDidntLoadWeDid;
			return;
		}
	}
	else
	{
		// Windows mapped the image
		if(winImageData != nullptr)
		{
			ImageCompare.compareResult = ImagesWindowsLoadedWeDidnt;
			return;
		}
	}

	// Both Windows and our image are the same
	ImageCompare.compareResult = ImagesEqual;
	ImageCompare.differenceOffset = 0;
}
