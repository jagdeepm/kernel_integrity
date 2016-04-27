#ifndef ELFFILE_H
#define ELFFILE_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <memory>
#include <sys/mman.h>
#include <vector>

class ElfLoader;
class ElfModuleLoader;
class ElfKernelLoader;
class ElfUserspaceLoader;
class Kernel;
class Process;
class SymbolManager;
class Process;

/* This class represents a symbol a loader may export for relocation */
class RelSym {
public:
	RelSym();
	RelSym(const std::string &name,
	       uint64_t value,
	       uint8_t info,
	       uint32_t shndx);
	~RelSym();

	std::string name;     ///< name of the symbol
	uint64_t value;       ///< final vaddr after loading
	uint8_t info;         ///< corresponding type and sym in parent
	uint32_t shndx;       ///< linked section index in parent
};


class SectionInfo {
public:
	SectionInfo();
	SectionInfo(const std::string &name,
	            uint32_t segID,
	            uint64_t offset,
	            uint8_t *index,
	            uint64_t memindex,
	            uint32_t size);
	virtual ~SectionInfo();

	std::string name;     ///< name of the section, init with first sec name
	uint32_t segID;       ///< section ID in section header table (SHT)
	uint64_t offset;      ///< section offset from beginning of ELF file
	uint8_t *index;       ///< section offset actual data pointer,
	                      ///< equals &elffilecontent[offset]
	uint64_t memindex;    ///< target virtual address within the VM
	uint32_t size;        ///< size of the section content

	bool containsElfAddress(uint64_t address);
	bool containsMemAddress(uint64_t address);
};


class SegmentInfo {
public:
	SegmentInfo();
	SegmentInfo(uint32_t p_type,
	            uint32_t p_flags,
	            uint64_t p_offset,
	            uint64_t p_vaddr,
	            uint64_t p_paddr,
	            uint64_t p_filesz,
	            uint64_t p_memsz,
	            uint64_t p_align);

	virtual ~SegmentInfo();

	uint32_t type;       ///< segment type
	uint32_t flags;      ///< segment flags
	uint64_t offset;     ///< byte offset where section starts in file
	uint64_t vaddr;      ///< virtual address of segment
	uint64_t paddr;      ///< physical address of segment (kernel thing)
	uint64_t filesz;     ///< size of segment in in file
	uint64_t memsz;      ///< size of segment in memory (larger!)
	uint64_t align;      ///< segment alignment
};

class ElfFile {
public:
	enum class ElfType { ELFTYPENONE, ELFTYPE32, ELFTYPE64 };

	enum class ElfProgramType {
		ELFPROGRAMTYPENONE,
		ELFPROGRAMTYPEKERNEL,
		ELFPROGRAMTYPEMODULE,
		ELFPROGRAMTYPEEXEC  //!< Type for loading executables
	};

	virtual ~ElfFile();

	virtual unsigned int getNrOfSections() const = 0;

	virtual SectionInfo findSectionWithName(const std::string &sectionName) const = 0;
	virtual SectionInfo findSectionByID(uint32_t sectionID) const = 0;
	virtual bool isCodeAddress(uint64_t address) = 0;
	virtual bool isDataAddress(uint64_t address) = 0;
	virtual std::string sectionName(int sectionID) const = 0;

	virtual SegmentInfo findCodeSegment() = 0;
	virtual SegmentInfo findDataSegment() = 0;

	virtual uint64_t findAddressOfVariable(const std::string &symbolName) = 0;

	virtual uint8_t *sectionAddress(int sectionID) = 0;
	virtual uint64_t sectionAlign(int sectionID) = 0;

	virtual std::string symbolName(uint32_t index, uint32_t strindex) const = 0;
	virtual void addSymbolsToStore(SymbolManager *store, uint64_t memindex) const = 0;

	virtual ElfType getType();
	virtual ElfProgramType getProgramType();

	std::string getFilename();
	void printSymbols(uint32_t symindex);

	uint8_t *getFileContent();
	size_t getFileSize();

	int getFD();

	/**
	 * Elffile-from-buffer factory method.
	 */
	static ElfFile *loadElfFileFromBuffer(uint8_t *buf, size_t size);

	/**
	 * Main elffile factory method.
	 */
	static ElfFile *loadElfFile(const std::string &filename);

	/**
	 * Parse this elf file as a kernel blob.
	 */
	virtual ElfKernelLoader *parseKernel() = 0;

	/**
	 * Parse this elf file as a kernel module associated with a given kernel.
	 */
	virtual ElfModuleLoader *parseKernelModule(const std::string &name,
	                                           Kernel *kernel) = 0;

	/**
	 * Parse this elf file as a executable/library
	 * associated with a given process.
	 */
	virtual ElfUserspaceLoader *parseUserspace(const std::string &name,
	                                           Kernel *kernel) = 0;

	virtual bool isRelocatable() const = 0;
	virtual void applyRelocations(ElfLoader *loader,
	                              Kernel *kernel,
	                              Process *process=nullptr) = 0;
	virtual bool isDynamic() const = 0;
	virtual bool isDynamicLibrary() const = 0;
	virtual bool isExecutable() const = 0;

	virtual std::vector<std::string> getDependencies() = 0;
	virtual std::vector<RelSym> getSymbols() = 0;

	virtual std::vector<Elf64_Rel> getRelEntries() const = 0;
	virtual std::vector<Elf64_Rela> getRelaEntries() const = 0;

	uint32_t shstrindex;

	SymbolManager *symbols;

protected:
	ElfFile(FILE *fd,
	        size_t fileSize,
	        uint8_t *fileContent,
	        ElfType type,
	        ElfProgramType programType);

	/**
	 * Parse the dwarf information from the binary.
	 */
	void parseDwarf();

	FILE *fd;
	size_t fileSize;
	uint8_t *fileContent;
	ElfType type;
	ElfProgramType programType;

	std::string filename;

	typedef std::map<std::string, uint64_t> SymbolNameMap;
	SymbolNameMap symbolNameMap;

	bool doLazyBind;
};

#include "elffile64.h"

#endif /* ELFFILE_H */
