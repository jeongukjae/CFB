#pragma once

#include <cmath>
#include <codecvt>
#include <cstdint>
#include <cstring>
#include <functional>
#include <locale>
#include <string>
#include <vector>

#define CFB_SIGNATURE "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"

#define CFB_SECTOR_MAX_REGULAR_SECTOR 0xFFFFFFFA  // Maximum regular sector number
#define CFB_SECTOR_NOT_APPLICABLE     0xFFFFFFFB  // Reserved for future use
#define CFB_SECTOR_DIFAT_SECTOR       0xFFFFFFFC  // Specifies a DIFAT sector in the FAT.
#define CFB_SECTOR_FAT_SECTOR         0xFFFFFFFD  // Specifies a FAT sector in the FAT.
#define CFB_SECTOR_END_OF_CHAIN       0xFFFFFFFE  // End of a linked chain of sectors.
#define CFB_SECTOR_FREE_SECT          0xFFFFFFFF  // Specifies an unallocated sector in the FAT, Mini FAT, or DIFAT.

#define CFB_DIRECTORY_ENTRY_NO_STREAM 0xFFFFFFFF

namespace CFB {

#pragma pack(push)
#pragma pack(1)

struct CompoundFileHeader {
  unsigned char signature[8];
  unsigned char unused_classId[16];
  uint16_t minorVersion;
  uint16_t majorVersion;
  uint16_t byteOrder;
  uint16_t sectorShift;
  uint16_t miniSectorShift;
  unsigned char reserved[6];
  uint32_t numDirectorySector;
  uint32_t numFATSector;
  uint32_t firstDirectorySectorLocation;
  uint32_t transactionSignatureNumber;
  uint32_t miniStreamCutoffSize;
  uint32_t firstMiniFATSectorLocation;
  uint32_t numMiniFATSector;
  uint32_t firstDIFATSectorLocation;
  uint32_t numDIFATSector;
  uint32_t headerDIFAT[109];
};

// total size: 128 bytes
struct DirectoryEntry {
  char16_t name[32];
  uint16_t nameLen;
  uint8_t objectType;
  uint8_t colorFlag;
  uint32_t leftSiblingID;
  uint32_t rightSiblingID;
  uint32_t childID;
  unsigned char clsid[16];
  uint32_t stateBits;
  uint64_t creationTime;
  uint64_t modifiedTime;
  uint32_t startSectorLocation;
  uint64_t streamSize;
};

struct PropertySetStreamHeader {
  unsigned char byteOrder[2];
  uint16_t version;
  uint32_t systemIdentifier;
  unsigned char clsid[16];
  uint32_t numPropertySets;
  struct {
    char fmtid[16];
    uint32_t offset;
  } propertySetInfo[1];
};

struct PropertySetHeader {
  uint32_t size;
  uint32_t NumProperties;
  struct {
    uint32_t id;
    uint32_t offset;
  } propertyIdentifierAndOffset[1];
};

#pragma pack(pop)

namespace internal {

inline std::string convertUTF16ToUTF8(const char16_t* u16Array) {
  std::u16string str(u16Array);
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
  std::string result = convert.to_bytes(str);

  return result;
}

}  // namespace internal

enum DirectoryEntryType {
  ENTRY_UNKNOWN_OR_UNALLOCATED = 0,
  ENTRY_STORAGE_OBJECT = 1,
  ENTRY_STREAM_OBJECT = 2,
  ENTRY_ROOT_STORAGE_OBJECT = 5,
};

class CompoundFile {
 public:
  CompoundFile() : buffer(nullptr), bufferLength(0), compoundFileHeader(nullptr), sectorSize(0), miniSectorSize(0), miniStreamStartSector(0) {}
  ~CompoundFile() {}

  void clear() {
    buffer = nullptr;
    bufferLength = 0;
    compoundFileHeader = nullptr;
    sectorSize = 0;
    miniSectorSize = 0;
    miniStreamStartSector = 0;
  }

  void read(const void* buffer, size_t bufferLength) {
    clear();

    if (buffer == NULL || bufferLength < sizeof(CompoundFileHeader))
      throw std::invalid_argument("Buffer is NULL or Buffer Length is zero.");

    this->buffer = reinterpret_cast<const unsigned char*>(buffer);
    this->bufferLength = bufferLength;

    compoundFileHeader = reinterpret_cast<const CompoundFileHeader*>(buffer);
    validateHeader();
    sectorSize = 1 << compoundFileHeader->sectorShift;
    miniSectorSize = 1 << compoundFileHeader->miniSectorShift;

    auto rootDirectoryEntry = getRootDirectoryEntry();
    if (rootDirectoryEntry->creationTime != 0)
      throw std::runtime_error("The Creation Time field in the root directory entry must be zero.");
    miniStreamStartSector = rootDirectoryEntry->startSectorLocation;
  }

  const CompoundFileHeader* getCompoundFileHeader() const { return compoundFileHeader; }

  const DirectoryEntry* getRootDirectoryEntry() const { return getDirectoryEntry(0); }

  // Get Directory Entry
  //
  //
  const DirectoryEntry* getDirectoryEntry(size_t directoryEntryId) const {
    if (directoryEntryId == CFB_DIRECTORY_ENTRY_NO_STREAM)
      return nullptr;

    uint32_t sectorNumber = compoundFileHeader->firstDirectorySectorLocation;
    uint32_t entriesPerSector = sectorSize / sizeof(DirectoryEntry);

    while ((entriesPerSector <= directoryEntryId) && (sectorNumber != CFB_SECTOR_END_OF_CHAIN)) {
      directoryEntryId -= entriesPerSector;
      sectorNumber = getNextFATSectorNumber(sectorNumber);
    }

    auto directoryEntryAddress = getAddressWithSectorNumber(sectorNumber, directoryEntryId * sizeof(DirectoryEntry));
    return reinterpret_cast<const DirectoryEntry*>(directoryEntryAddress);
  }

  std::vector<char> readStreamOfEntry(const DirectoryEntry* entry) const {
    std::vector<char> buffer;
    readStreamOfEntry(entry, buffer);
    return buffer;
  }

  void readStreamOfEntry(const DirectoryEntry* entry, std::vector<char>& buffer) const {
    buffer.resize(entry->streamSize);

    if (entry->streamSize < compoundFileHeader->miniStreamCutoffSize)
      readStream(entry->startSectorLocation, entry->streamSize, buffer, miniSectorSize,
                 std::bind(&CompoundFile::getAddressWithMiniSectorNumber, this, std::placeholders::_1, std::placeholders::_2),
                 std::bind(&CompoundFile::getNextMiniFATSectorNumber, this, std::placeholders::_1));
    else
      readStream(entry->startSectorLocation, entry->streamSize, buffer, sectorSize,
                 std::bind(&CompoundFile::getAddressWithSectorNumber, this, std::placeholders::_1, std::placeholders::_2),
                 std::bind(&CompoundFile::getNextFATSectorNumber, this, std::placeholders::_1));
  }

  template <typename CallbackType>
  void iterateAll(CallbackType callback) const {
    iterateNodes(getDirectoryEntry(getRootDirectoryEntry()->childID), 0, callback);
  }

  template <typename CallbackType>
  void iterateFromDirectoryEntry(const DirectoryEntry* directoryEntry, CallbackType callback) const {
    iterateNodes(getDirectoryEntry(directoryEntry->childID), 0, callback);
  }

  static bool isStreamObject(const DirectoryEntry* directoryEntry) { return directoryEntry->objectType == ENTRY_STREAM_OBJECT; }
  static bool isStorageObject(const DirectoryEntry* directoryEntry) { return directoryEntry->objectType == ENTRY_STORAGE_OBJECT; }
  // https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-oleps/e5484a83-3cc1-43a6-afcf-6558059fe36e
  // check is property set stream
  static bool isPropertySetStream(const DirectoryEntry* directoryEntry) { return directoryEntry->name[0] == 0x05; }

 private:
  const unsigned char* buffer;
  size_t bufferLength;
  const CompoundFileHeader* compoundFileHeader;
  size_t sectorSize;
  size_t miniSectorSize;
  size_t miniStreamStartSector;

  // Get pointer for sector number and offset
  const void* getAddressWithSectorNumber(uint32_t sectorNumber, uint32_t offset = 0) const {
    if (offset >= sectorSize)
      throw std::invalid_argument("getAddressWithSectorNumber : offset >= sectorSize");

    if (sectorNumber >= CFB_SECTOR_MAX_REGULAR_SECTOR)
      throw std::invalid_argument("getAddressWithSectorNumber : sectorNumber >= CFB_SECTOR_MAX_REGULAR_SECTOR");

    // A sector #0 of the file begins at byte offset Sector Size, not at 0.
    uint64_t bufferOffset = sectorSize * (sectorNumber + 1) + offset;
    if (bufferLength <= bufferOffset)
      throw std::runtime_error("Trying to access out of file");

    return buffer + bufferOffset;
  }

  const void* getAddressWithMiniSectorNumber(uint32_t sectorNumber, uint32_t offset = 0) const {
    if (offset >= miniSectorSize)
      throw std::invalid_argument("getAddressWithSectorNumber : offset >= miniSectorSize, offset: " + std::to_string(offset));

    if (sectorNumber >= CFB_SECTOR_MAX_REGULAR_SECTOR)
      throw std::invalid_argument("getAddressWithSectorNumber : sectorNumber >= CFB_SECTOR_MAX_REGULAR_SECTOR");

    // A mini FAT sector number can be converted into a byte offset into the mini stream by using the following formula: sector number x 64 bytes.
    auto desiredSector = miniStreamStartSector;
    offset = sectorNumber * miniSectorSize + offset;

    while (offset >= sectorSize) {
      desiredSector = getNextFATSectorNumber(desiredSector);
      offset -= sectorSize;
    }

    return getAddressWithSectorNumber(desiredSector, offset);
  }

  static uint32_t getUint32Field(const void* address) { return *reinterpret_cast<const uint32_t*>(address); }

  // Validate Header when reading a file.
  //
  // 1. check signature
  // 2. check minor version and major version with sector shift
  // 3. check byte order
  // 4. check mini sector shift
  void validateHeader() const {
    if (std::memcmp(compoundFileHeader->signature, CFB_SIGNATURE, 8) != 0)
      throw std::runtime_error("Invalid CFB Signature.");

    if (compoundFileHeader->minorVersion != 0x003E)
      throw std::runtime_error("Minor Version is not 0x003E");

    if (compoundFileHeader->majorVersion != 0x0003 && compoundFileHeader->majorVersion != 0x0004)
      throw std::runtime_error("Major Version should be 3 or 4");

    // If major version is 3, sector shift must be 9, (sector size = 512 bytes)
    // If major version is 4, sector shift must be C, (sector size = 4096 bytes)
    if (((compoundFileHeader->majorVersion == 0x003) ^ (compoundFileHeader->sectorShift == 0x0009)) ||
        (compoundFileHeader->majorVersion == 0x004) ^ (compoundFileHeader->sectorShift == 0x000C))
      throw std::runtime_error("Invalid Sector Shift");

    if (compoundFileHeader->byteOrder != 0xFFFE)
      throw std::runtime_error("Invalid Byte Order");

    if (compoundFileHeader->miniSectorShift != 0x0006)
      throw std::runtime_error("Invalid mini sector shift");
  }

  // Find the next mini sector number by lookup FAT sector.
  //
  // Mini FAT sectors are sotred in the FAT, with the starting location of chain stored in the header.
  uint32_t getNextMiniFATSectorNumber(size_t sectorNumber) const {
    uint32_t entriesPerSector = sectorSize / 4;

    uint32_t miniFATSectorNumber = compoundFileHeader->firstMiniFATSectorLocation;
    while (sectorNumber >= entriesPerSector && miniFATSectorNumber != CFB_SECTOR_END_OF_CHAIN) {
      sectorNumber -= entriesPerSector;
      miniFATSectorNumber = getNextFATSectorNumber(miniFATSectorNumber);
    }

    if (miniFATSectorNumber == CFB_SECTOR_END_OF_CHAIN)
      return CFB_SECTOR_END_OF_CHAIN;

    auto nextMiniSectorNumber = getUint32Field(getAddressWithSectorNumber(miniFATSectorNumber, sectorNumber * 4));
    return nextMiniSectorNumber;
  }

  // Find the next sector number by lookup FAT sectors.
  //
  // Each entry in FAT Sectors contains the sector number of the next sector in the chain.
  // So we can find next sector by lookup desired index in FAT.
  uint32_t getNextFATSectorNumber(size_t sectorNumber) const {
    // 4 = size of a entry (32-bit)
    uint32_t entriesPerSector = sectorSize / 4;
    uint32_t currentFATSector = sectorNumber / entriesPerSector;

    auto currentFATSectorNumber = getFATSectorNumber(currentFATSector);
    auto nextSectorNumber = getUint32Field(getAddressWithSectorNumber(currentFATSectorNumber, (sectorNumber % entriesPerSector) * 4));
    return nextSectorNumber;
  }

  // Get FAT Sector number by lookup the DIFAT Array in a header and DIFAT Sectors
  //
  // TODO: check END_OF_CHAIN Values
  uint32_t getFATSectorNumber(size_t sectorNumber) const {
    // In the header, the DIFAT array occupies 109 entries
    if (sectorNumber < 109)
      return compoundFileHeader->headerDIFAT[sectorNumber];

    // In each DIFAT sector, the DIFAT array occupies the entire sector minus 4 bytes.
    // The last 4 bytes is for chaining the DIFAT sector chain. (Next DIFAT Sector Location)
    size_t entriesPerSector = sectorSize / 4 - 1;
    sectorNumber -= 109;
    uint32_t difatSectorNumber = compoundFileHeader->firstDIFATSectorLocation;

    // If desired sector number is not contained current DIFAT Sector, lookup next DIFAT Sector.
    while (sectorNumber >= entriesPerSector) {
      sectorNumber -= entriesPerSector;
      // In DIFAT Sectors, "Next DIFAT Sector Location" field is at the last.
      difatSectorNumber = getUint32Field(getAddressWithSectorNumber(difatSectorNumber, sectorSize - 4));
    }

    return getUint32Field(getAddressWithSectorNumber(difatSectorNumber, sectorNumber * 4));
  }

  template <typename CallbackType>
  void iterateNodes(const DirectoryEntry* entry, size_t depth, CallbackType callback) const {
    if (entry == nullptr)
      return;

    callback(entry, depth);

    const DirectoryEntry* child = getDirectoryEntry(entry->childID);
    if (child != nullptr)
      iterateNodes(getDirectoryEntry(entry->childID), depth + 1, callback);

    iterateNodes(getDirectoryEntry(entry->leftSiblingID), depth, callback);
    iterateNodes(getDirectoryEntry(entry->rightSiblingID), depth, callback);
  }

  void readStream(uint32_t sectorNumber,
                  uint64_t streamSizeToRead,
                  std::vector<char>& buffer,
                  uint64_t sectorSize,
                  std::function<const void*(uint32_t, uint32_t)> addressFn,
                  std::function<uint32_t(uint32_t)> nextSectorFn) const {
    size_t bufferPosition = 0;

    while (streamSizeToRead > 0) {
      const void* sourceAddress = addressFn(sectorNumber, 0);
      auto streamSizeToCopy = std::min(sectorSize, streamSizeToRead);
      memcpy(buffer.data() + bufferPosition, sourceAddress, std::min(sectorSize, streamSizeToCopy));

      bufferPosition += streamSizeToCopy;
      streamSizeToRead -= streamSizeToCopy;
      sectorNumber = nextSectorFn(sectorNumber);
    }
  }
};

}  // namespace CFB
