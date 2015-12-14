#ifndef ELFFILE64_H
#define ELFFILE64_H

#include <elffile.h>

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

#include <fcntl.h>
#include <cstring>

#include <sys/mman.h>

#include <map>

class ElfFile64 : public ElfFile {
public:
	ElfFile64(FILE* fd, size_t fileSize, uint8_t* fileContent);
	virtual ~ElfFile64();

	int getNrOfSections() override;

	SectionInfo findSectionWithName(const std::string &sectionName) const override;
	SectionInfo findSectionByID(uint32_t sectionID) const override;
	bool isCodeAddress(uint64_t address) override;
	bool isDataAddress(uint64_t address) override;

	std::string sectionName(int sectionID) override;
	uint8_t *sectionAddress(int sectionID) override;
	uint64_t sectionAlign(int sectionID) override;

	SegmentInfo findCodeSegment() override;
	SegmentInfo findDataSegment() override;

	std::string symbolName(uint32_t index, uint32_t strindex) const override;
	void addSymbolsToKernel(Kernel *kernel, uint64_t memindex) const override;

	uint64_t findAddressOfVariable(const std::string &symbolName) override;

	ElfKernelLoader *parseKernel() override;
	ElfModuleLoader *parseKernelModule(const std::string &name,
	                                   Kernel *kernel) override;
	ElfProcessLoader *parseProcess(const std::string &name,
	                               Process *process,
	                               Kernel *kernel) override;


	bool isRelocatable() const override;
	void applyRelocations(ElfLoader *loader,
	                      Kernel *kernel,
	                      Process *process) override;
	bool isDynamic() const override;
	bool isDynamicLibrary() const override;
	bool isExecutable() const override;

	std::vector<RelSym> getSymbols() override;

	std::vector<std::string> getDependencies() override;


	Elf64_Ehdr *elf64Ehdr;
	Elf64_Shdr *elf64Shdr;
	Elf64_Phdr *elf64Phdr;

	template<typename T>
	void getRelEntries(std::vector<T> &ret, uint32_t type);
	void getRelEntries(std::vector<Elf64_Rel> &ret) override;
	void getRelaEntries(std::vector<Elf64_Rela> &ret) override;

private:
	void applyRelaOnSection(uint32_t relSectionID,
	                        ElfLoader *loader,
	                        Kernel *kernel,
	                        Process *process);
};

#endif /* ELFFILE64_H */
