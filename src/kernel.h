#ifndef KERNELMANAGER_H
#define KERNELMANAGER_H

#include <iostream>
#include <list>
#include <map>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "libdwarfparser/symbolmanager.h"
#include "libdwarfparser/instance.h"
#include "libvmiwrapper/libvmiwrapper.h"

#include "paravirt_state.h"
#include "userspacemanager.h"
#include "taskmanager.h"

class ElfLoader;

class Kernel {
public:
	Kernel();
	virtual ~Kernel() = default;

	void setKernelDir(const std::string &dirName);

	void setVMIInstance(VMIInstance *vmi);

	void loadKernelModules();
	std::list<std::string> getKernelModules();
	Instance getKernelModuleInstance(std::string modName);

	void loadAllModules();
	void loadModuleThread(std::list<std::string> &modList,
	                      std::mutex &modMutex);
	ElfLoader *loadModule(const std::string &moduleName);
	void parseSystemMap();

	ParavirtState *getParavirtState();
	UserspaceManager* getUserspace();
	TaskManager *getTaskManager();

	void initTaskManager();

	VMIInstance *vmi;

	/**
	 * Symbol position tracking and querying.
	 */
	SymbolManager symbols;

protected:
	ParavirtState paravirt;
	UserspaceManager userspace;
	TaskManager tm;

	std::mutex moduleMapMutex;
	typedef std::unordered_map<std::string, ElfLoader*> ModuleMap;
	ModuleMap moduleMap;

private:
	std::string kernelDirName;

	typedef std::unordered_map<std::string, Instance> ModuleInstanceMap;
	ModuleInstanceMap moduleInstanceMap;

	Instance nextModule(Instance &instance);
	std::string findModuleFile(std::string modName) const;
};


#endif  /* KERNELMANAGER_H */
