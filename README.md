# CFB

![C++ Test](https://github.com/jeongukjae/CFB/workflows/C++%20Test/badge.svg)
[![codecov](https://codecov.io/gh/jeongukjae/CFB/branch/master/graph/badge.svg)](https://codecov.io/gh/jeongukjae/CFB)

A single header C++ library for Compound File Binary(CFB) Format

## Usage

You can check [test codes](./tests/CFB_test.cc)

### Read Compound File, Iterate All Directory Entries, and Read Stream Data of Specific Entry

```c++
  std::vector<char> bytes = ...

  CFB::CompoundFile file;
  file.read(bytes.data(), bytes.size());

  td::map<std::string, const CFB::DirectoryEntry*> entries;
  file.iterateAll([&entries](const CFB::DirectoryEntry* entry, size_t depth) {
    // You can use CFB::internal::convertUTF16ToUTF8 function temporarily to convert u16string to std::string
    std::cout << "Depth:" << depth << ", name: " << CFB::internal::convertUTF16ToUTF8(entry->name) << std::endl;
    entries[CFB::internal::convertUTF16ToUTF8(entry->name)] = entry;
  });

  std::vector<char> data = file.readStreamOfEntry(entries["SOME_DIRECTORY_ENTRY_NAME"]);
```

### Parsing Property Set

```c++
  std::vector<char> bytes = ...

  CFB::CompoundFile file;
  file.read(bytes.data(), bytes.size());

  const CFB::DirectoryEntry* propertyEntry;
  file.iterateAll([&propertyEntry](const CFB::DirectoryEntry* entry, size_t depth) {
    if (CFB::isPropertySetStream(entry))
      propertyEntry = entry;
  });

  std::vector<char> streamOfPropertyEntry = file.readStreamOfEntry(propertyEntry);
  CFB::PropertySetStream propertySetStream(streamOfPropertyEntry.data(), streamOfPropertyEntry.size());

  // check num property sets
  EXPECT_EQ(propertySetStream.getNumPropertySets(), 1);

  // get first property set
  CFB::PropertySet propertySet = propertySetStream.getPropertySet(0);
  // Get property by Id
  const CFB::TypedPropertyValue* property = propertySet.getPropertyById(3);
  // You can convert VT_LPSTR to char16_t array using below function.
  const char16_t* utf16string = CFB::VT::getCodePageString(property);
  // And convert it to std::string
  const std::string result = CFB::internal::convertUTF16ToUTF8(utf16string);

  EXPECT_EQ(CFB::internal::convertUTF16ToUTF8(CFB::VT::getCodePageString(propertySet.getPropertyById(3))),
            "http://vstfbing:8080/tfs/Bing/Bing/_workitems#path=Shared+Queries%2FSTC-A%2FMarketEngagement%2FMobile+Browser%2FMobileBrowser+Active+Bugs&_a=query");
  EXPECT_EQ(CFB::internal::convertUTF16ToUTF8(CFB::VT::getCodePageString(propertySet.getPropertyById(5))),
            "MobileBrowser Active Bugs - Microsoft Team Foundation Server");
  EXPECT_EQ(CFB::internal::convertUTF16ToUTF8(CFB::VT::getCodePageString(propertySet.getPropertyById(1002))),
            "http://vstfbing:8080/tfs/favicon.ico");
```

## References

- [[MS-CFB]: Compound File Binary File Format](https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/53989ce4-7b05-4f8d-829b-d08d6148375b)
- [[MS-OLEPS]: Object Linking and Embedding (OLE) Property Set Data Structures](https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-oleps/bf7aeae8-c47a-4939-9f45-700158dac3bc)
- [microsoft/compoundfilereader](https://github.com/microsoft/compoundfilereader)
