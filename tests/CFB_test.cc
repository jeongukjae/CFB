#include "CFB.hh"
#include <fstream>
#include <map>
#include <vector>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

std::vector<char> readFile(const std::string filename) {
  std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
  std::ifstream::pos_type pos = ifs.tellg();

  std::vector<char> result(pos);

  ifs.seekg(0, std::ios::beg);
  ifs.read(&result[0], pos);

  return result;
}

TEST(CFB_read, check_file_header) {
  auto bytes = readFile("../tests/data/1.dat");

  CFB::CompoundFile file;
  file.read(bytes.data(), bytes.size());

  auto header = file.getCompoundFileHeader();

  EXPECT_EQ(header->majorVersion, 3);
  EXPECT_EQ(header->minorVersion, 0x3E);
  EXPECT_EQ(header->numDIFATSector, 0);
  EXPECT_EQ(header->numFATSector, 1);
  EXPECT_EQ(header->numMiniFATSector, 1);

  bytes = readFile("../tests/data/2.dat");
  file.read(bytes.data(), bytes.size());

  header = file.getCompoundFileHeader();

  EXPECT_EQ(header->majorVersion, 3);
  EXPECT_EQ(header->minorVersion, 0x3E);
  EXPECT_EQ(header->numDIFATSector, 0);
  EXPECT_EQ(header->numFATSector, 1);
  EXPECT_EQ(header->numMiniFATSector, 1);
}

TEST(CFB_read, check_iterate_nodes) {
  auto bytes = readFile("../tests/data/1.dat");

  CFB::CompoundFile file;
  file.read(bytes.data(), bytes.size());
  std::vector<std::string> names;
  file.iterateAll([&names](const CFB::DirectoryEntry* entry, size_t depth) { names.push_back(CFB::internal::convertUTF16ToUTF8(entry->name)); });

  EXPECT_EQ(names.size(), 5);
  EXPECT_THAT(names, testing::ElementsAre("\x5Xrpnqgkd0qyouogaTj5jpe4dEe", "TL1", "TL0", "TravelLog", "TL2"));

  bytes = readFile("../tests/data/2.dat");
  file.read(bytes.data(), bytes.size());
  names.clear();
  file.iterateAll([&names](const CFB::DirectoryEntry* entry, size_t depth) { names.push_back(CFB::internal::convertUTF16ToUTF8(entry->name)); });

  EXPECT_EQ(names.size(), 4);
  EXPECT_THAT(names, testing::ElementsAre("TravelLog", "TL0", "TL1", "\x5Xrpnqgkd0qyouogaTj5jpe4dEe"));
}

TEST(CFB_read, check_mini_stream_content) {
  auto bytes = readFile("../tests/data/1.dat");

  CFB::CompoundFile file;
  file.read(bytes.data(), bytes.size());
  std::map<std::string, const CFB::DirectoryEntry*> entries;
  file.iterateAll([&entries](const CFB::DirectoryEntry* entry, size_t depth) { entries[CFB::internal::convertUTF16ToUTF8(entry->name)] = entry; });

  EXPECT_EQ(entries.size(), 5);
  EXPECT_NE(entries.find("TravelLog"), entries.end());
  EXPECT_EQ(entries["TravelLog"]->streamSize, 12);
  auto data = file.readStreamOfEntry(entries["TravelLog"]);
  EXPECT_EQ(data.size(), 12);
  EXPECT_THAT(data, testing::ElementsAre('\x00', '\x00', '\x00', '\x00', '\x01', '\x00', '\x00', '\x00', '\x02', '\x00', '\x00', '\x00'));

  EXPECT_NE(entries.find("TL0"), entries.end());
  EXPECT_EQ(entries["TL0"]->streamSize, 526);
  data = file.readStreamOfEntry(entries["TL0"]);
  EXPECT_EQ(data.size(), 526);
  // Check First 80 elements
  EXPECT_THAT(std::vector<char>(data.begin(), data.begin() + 80),
              testing::ElementsAreArray("\x54\x01\x14\x00\x1f\x00\x80\x53\x1c\x87\xa0\x42\x69\x10\xa2\xea\x08\x00\x2b\x30"
                                        "\x30\x9d\x3e\x01\x61\x80\x00\x00\x00\x00\x68\x00\x74\x00\x74\x00\x70\x00\x3a\x00"
                                        "\x2f\x00\x2f\x00\x76\x00\x73\x00\x74\x00\x66\x00\x62\x00\x69\x00\x6e\x00\x67\x00"
                                        "\x3a\x00\x38\x00\x30\x00\x38\x00\x30\x00\x2f\x00\x74\x00\x66\x00\x73\x00\x2f\x00",
                                        80));
}

TEST(CFB_read, check_stream_content) {
  auto bytes = readFile("../tests/data/한글문서파일형식_5.0_revision1.3.hwp");

  CFB::CompoundFile file;
  file.read(bytes.data(), bytes.size());
  std::map<std::string, const CFB::DirectoryEntry*> entries;
  file.iterateAll([&entries](const CFB::DirectoryEntry* entry, size_t depth) { entries[CFB::internal::convertUTF16ToUTF8(entry->name)] = entry; });

  EXPECT_EQ(entries["PrvImage"]->streamSize, 48142);
  EXPECT_EQ(entries["PrvImage"]->startSectorLocation, 12);
  auto data = file.readStreamOfEntry(entries["PrvImage"]);

  EXPECT_EQ(data.size(), 48142);
  EXPECT_THAT(std::vector<char>(data.begin(), data.begin() + 80),
              testing::ElementsAreArray("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x02\xd4"
                                        "\x00\x00\x04\x00\x08\x06\x00\x00\x00\xc1\x52\xce\xf3\x00\x00\x00\x01\x73\x52\x47"
                                        "\x42\x00\xae\xce\x1c\xe9\x00\x00\x00\x04\x67\x41\x4d\x41\x00\x00\xb1\x8f\x0b\xfc"
                                        "\x61\x05\x00\x00\x00\x09\x70\x48\x59\x73\x00\x00\x0e\xc3\x00\x00\x0e\xc3\x01\xc7",
                                        80));
  EXPECT_THAT(std::vector<char>(data.end() - 78, data.end()),
              testing::ElementsAreArray("\x10\xa8\x01\x00\x00\x00\x17\x08\xd4\x00\x00\x00\x80\x0b\x04\x6a\x00\x00\x00\xc0"
                                        "\x05\x02\x35\x00\x00\x00\xe0\x02\x81\x1a\x00\x00\x00\x70\x81\x40\x0d\x00\x00\x00"
                                        "\xb8\x40\xa0\x06\x00\x00\x00\x5c\x20\x50\x03\x00\x00\x00\x79\x26\xf2\xff\x0b\x9d"
                                        "\x28\x66\xac\x1e\x14\x29\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82 ",
                                        78));
}
