#ifndef ELFFILE_H
#define ELFFILE_H

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

#include <fcntl.h>
#include <cstring>

#include <sys/mman.h>

#include <map>
#include <vector>

class ElfLoader;
class ElfModuleLoader;
class KernelManager;

class SectionInfo{

public:
	SectionInfo();
	SectionInfo(std::string segName, uint32_t segID, uint8_t * i, uint64_t a, uint32_t s);
	virtual ~SectionInfo();

	std::string segName;        // name of the segment, init with first sec name
	uint32_t    segID;          // section ID in SHT
	uint8_t *   index;          // section offset from beginning of ELF file
	// if dereferenced contains data of the section
	uint8_t *   memindex;       // target virtual address in process image
	uint32_t    size;           // size of the section content (in bytes?)

	bool containsElfAddress(uint64_t address);
	bool containsMemAddress(uint64_t address);

private:
	SectionInfo(uint8_t * i, uint32_t s);
};

class SegmentInfo{
public:
	SegmentInfo();
	SegmentInfo(
		uint32_t   p_type,
		uint32_t   p_flags,
		uint64_t   p_offset,
		uint8_t*   p_vaddr,
		uint8_t*   p_paddr,
		uint64_t   p_filesz,
		uint64_t   p_memsz,
		uint64_t   p_align);

	virtual ~SegmentInfo();

	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint8_t* vaddr;
	uint8_t* paddr;
	uint64_t filesz;
	uint64_t memsz;
	uint64_t align;
};


class ElfFile{
public:
	typedef enum {
		ELFTYPENONE = 0,
		ELFTYPE32,
		ELFTYPE64
	} ElfType;

	typedef enum {
		ELFPROGRAMTYPENONE = 0,
		ELFPROGRAMTYPEKERNEL,
		ELFPROGRAMTYPEMODULE,
		ELFPROGRAMTYPEEXEC      // NEW: Type for loading executables
	} ElfProgramType;

	virtual ~ElfFile();

	virtual int getNrOfSections() = 0;

	virtual SectionInfo findSectionWithName(std::string sectionName) = 0;
	virtual SectionInfo findSectionByID(uint32_t sectionID) = 0;
	virtual bool isCodeAddress(uint64_t address) = 0;
	virtual bool isDataAddress(uint64_t address) = 0;
	virtual std::string sectionName(int sectionID) = 0;

	virtual SegmentInfo findCodeSegment() = 0;
	virtual SegmentInfo findDataSegment() = 0;

	virtual uint64_t findAddressOfVariable(std::string symbolName) = 0;

	virtual uint8_t *sectionAddress(int sectionID) = 0;
	virtual uint64_t sectionAlign(int sectionID) = 0;

	virtual std::string symbolName(uint32_t index) = 0;

	virtual ElfType getType();
	virtual ElfProgramType getProgramType();

	std::string getFilename();
	void printSymbols();

	uint8_t* getFileContent();
	size_t getFileSize();

	int getFD();

	static ElfFile* loadElfFileFromBuffer(uint8_t* buf, size_t size) throw();
	static ElfFile* loadElfFile(std::string filename) throw();
	virtual ElfLoader* parseElf(ElfFile::ElfProgramType type,
	                            std::string name = "",
	                            KernelManager* parent = 0) = 0;

	virtual bool isRelocatable() = 0;
	virtual void applyRelocations(ElfModuleLoader *loader) = 0;
	virtual bool isDynamic() = 0;
	virtual bool isExecutable() = 0;

	virtual std::vector<std::string> getDependencies() = 0;

	uint32_t shstrindex;
	uint32_t symindex;
	uint32_t strindex;

protected:

	ElfFile(FILE* fd, size_t fileSize, uint8_t* fileContent, ElfType type,
	        ElfProgramType programType);

	FILE* fd;
	size_t fileSize;
	uint8_t* fileContent;
	ElfType type;
	ElfProgramType programType;

	std::string filename;

	typedef std::map<std::string, uint64_t> SymbolNameMap;
	SymbolNameMap symbolNameMap;

};

#include <elffile32.h>
#include <elffile64.h>


#endif /* ELFFILE_H */
