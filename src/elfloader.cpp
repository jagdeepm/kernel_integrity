#include "helpers.h"
#include "elfloader.h"

#include "exceptions.h"
#include "kernel_headers.h"
#include <cassert>


#include <cstring>

#include <iostream>
#include <fstream>

#include <typeinfo>

#include "libdwarfparser/libdwarfparser.h"
#include "libvmiwrapper/libvmiwrapper.h"

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
//The following should replace boost filesystem once it is available in gcc
//#include <filesystem>
//namespace fs = std::filesystem;

ParavirtState::ParavirtState(){
	this->updateState();
}

ParavirtState::~ParavirtState(){}

void ParavirtState::updateState(){

    pv_init_ops = Variable::findVariableByName("pv_init_ops")->getInstance();
    pv_time_ops = Variable::findVariableByName("pv_time_ops")->getInstance();
    pv_cpu_ops  = Variable::findVariableByName("pv_cpu_ops" )->getInstance();
    pv_irq_ops  = Variable::findVariableByName("pv_irq_ops" )->getInstance();
    pv_apic_ops = Variable::findVariableByName("pv_apic_ops")->getInstance();
    pv_mmu_ops  = Variable::findVariableByName("pv_mmu_ops" )->getInstance();
    pv_lock_ops = Variable::findVariableByName("pv_lock_ops")->getInstance();

    
    Function* func = 0;

	func = Function::findFunctionByName("_paravirt_nop");
	assert(func);
	nopFuncAddress = func->getAddress();
	
	func = Function::findFunctionByName("_paravirt_ident_32");
	assert(func);
    ident32NopFuncAddress = func->getAddress();
	
	func = Function::findFunctionByName("_paravirt_ident_64");
	assert(func);
    ident64NopFuncAddress = func->getAddress();

	assert(nopFuncAddress);
	assert(ident32NopFuncAddress);
	assert(ident64NopFuncAddress);

    const Structured * pptS = 
		dynamic_cast<const Structured*>(
				BaseType::findBaseTypeByName("paravirt_patch_template"));
	assert(pptS);

    pv_irq_opsOffset = pptS->memberOffset("pv_irq_ops");
    pv_cpu_opsOffset = pptS->memberOffset("pv_cpu_ops");
	pv_mmu_opsOffset = pptS->memberOffset("pv_mmu_ops");
    
}

ElfLoader::ElfLoader(ElfFile* elffile):
	elffile(elffile),
	kernelModule(),
	textSegment(),
	textSegmentContent(),
	jumpTable(),
	dataSegment(),
	paravirtState(){

	this->ideal_nops = p6_nops;
}

ElfLoader::~ElfLoader(){}

void  ElfLoader::add_nops(void *insns, uint8_t len)
{
    while (len > 0) {
        unsigned int noplen = len;
        if (noplen > ASM_NOP_MAX)
            noplen = ASM_NOP_MAX;
        memcpy(insns, (void*) ideal_nops[noplen], noplen);
        insns = (void *) ((char*) insns + noplen);
        len -= noplen;
    }
}

uint8_t ElfLoader::paravirt_patch_nop(void) { return 0; }

uint8_t ElfLoader::paravirt_patch_ignore(unsigned len) { return len; }

uint8_t ElfLoader::paravirt_patch_insns(void *insnbuf, unsigned len,
                              const char *start, const char *end)
{
    uint8_t insn_len = end - start;

    if (insn_len > len || start == NULL)
        insn_len = len;
    else
        memcpy(insnbuf, start, insn_len);

    return insn_len;
}

uint8_t ElfLoader::paravirt_patch_jmp(void *insnbuf, uint64_t target, uint64_t addr, uint8_t len)
{
    if (len < 5) return len;

    uint32_t delta = target - (addr + 5);

    *((char*) insnbuf) = 0xe9;
    *((uint32_t*) ((char*) insnbuf + 1)) = delta;

    std::cout << "Patching jump @ " << std::hex << addr << std::dec << std::endl;

    return 5;
}

uint8_t ElfLoader::paravirt_patch_call(void *insnbuf, uint64_t target, uint16_t tgt_clobbers, uint64_t addr, uint16_t site_clobbers, uint8_t len)
{
    if (tgt_clobbers & ~site_clobbers) return len;
    if (len < 5) return len;

    uint32_t delta = target - (addr + 5);

    *((char*) insnbuf) = 0xe8;
    *((uint32_t*) ((char*) insnbuf + 1)) = delta;

    return 5;
}

uint64_t ElfLoader::get_call_destination(uint32_t type)
{
    //These structs contain a function pointers.
    //In memory they are directly after each other.
    //Thus type is an index into the resulting array.


    if(type < paravirtState.pv_init_ops.size()) 
		return paravirtState.pv_init_ops.memberByOffset(type)
		    .getRawValue<uint64_t>(false);
    type -= paravirtState.pv_init_ops.size();
    if(type < paravirtState.pv_time_ops.size()) 
		return paravirtState.pv_time_ops.memberByOffset(type)
		    .getRawValue<uint64_t>(false);
    type -= paravirtState.pv_time_ops.size();
    if(type < paravirtState.pv_cpu_ops.size())  
		return paravirtState.pv_cpu_ops .memberByOffset(type)
		    .getRawValue<uint64_t>(false);
    type -= paravirtState.pv_cpu_ops.size();
    if(type < paravirtState.pv_irq_ops.size())  
		return paravirtState.pv_irq_ops .memberByOffset(type)
		    .getRawValue<uint64_t>(false);
    type -= paravirtState.pv_irq_ops.size();
    if(type < paravirtState.pv_apic_ops.size()) 
		return paravirtState.pv_apic_ops.memberByOffset(type)
		    .getRawValue<uint64_t>(false);
    type -= paravirtState.pv_apic_ops.size();
    if(type < paravirtState.pv_mmu_ops.size())  
		return paravirtState.pv_mmu_ops .memberByOffset(type)
		    .getRawValue<uint64_t>(false);
    type -= paravirtState.pv_mmu_ops.size();
    if(type < paravirtState.pv_lock_ops.size()) 
		return paravirtState.pv_lock_ops.memberByOffset(type)
		    .getRawValue<uint64_t>(false);

    return 0;
}

uint8_t ElfLoader::paravirt_patch_default(uint32_t type, uint16_t clobbers, void *insnbuf,
                                uint64_t addr, uint8_t len)
{
    uint8_t ret = 0;
    //Get Memory of paravirt_patch_template + type
    uint64_t opfunc = get_call_destination(type);

    if (!opfunc)
    {
        // opfunc == NULL
        /* If there's no function, patch it with a ud2a (BUG) */
        //If this is a module this is a bug anyway so this should not happen.
        //ret = paravirt_patch_insns(insnbuf, len, ud2a, ud2a+sizeof(ud2a));
        //If this the kernel this can happen and is only filled with nops
        ret = paravirt_patch_nop();
    }
    //TODO get address of Function Paravirt nop
    else if (opfunc == paravirtState.nopFuncAddress){
        /* If the operation is a nop, then nop the callsite */
        ret = paravirt_patch_nop();
	}
    /* identity functions just return their single argument */
    else if (opfunc == paravirtState.ident32NopFuncAddress){
        ret = paravirt_patch_insns(insnbuf, len, start__mov32, end__mov32);
	}
    else if (opfunc == paravirtState.ident64NopFuncAddress){
        ret = paravirt_patch_insns(insnbuf, len, start__mov64, end__mov64);
	}
    else if (type == paravirtState.pv_cpu_opsOffset + 
				paravirtState.pv_cpu_ops.memberOffset("iret") ||
             type == paravirtState.pv_cpu_opsOffset + 
				paravirtState.pv_cpu_ops.memberOffset("irq_enable_sysexit") ||
             type == paravirtState.pv_cpu_opsOffset + 
				paravirtState.pv_cpu_ops.memberOffset("usergs_sysret32") ||
             type == paravirtState.pv_cpu_opsOffset + 
				paravirtState.pv_cpu_ops.memberOffset("usergs_sysret64"))
    {
        /* If operation requires a jmp, then jmp */
        //std::cout << "Patching jump!" << std::endl;
        ret = paravirt_patch_jmp(insnbuf, opfunc, addr, len);
        //TODO add Jump Target
		//if (!_paravirtJump.contains(opfunc))
        //{
        //    _paravirtJump.append(opfunc);
        //}
    }
    else
    {
        /* Otherwise call the function; assume target could
           clobber any caller-save reg */
        ret = paravirt_patch_call(insnbuf, opfunc, CLBR_ANY,
                                  addr, clobbers, len);
		//TODO add call target
        //if (!_paravirtCall.contains(opfunc))
        //{
        //    _paravirtCall.append(opfunc);
        //}
    }
    return ret;
}

uint32_t ElfLoader::paravirtNativePatch(uint32_t type, uint16_t clobbers, void *ibuf,
                             unsigned long addr, unsigned len)
{
    uint32_t ret = 0;


#define PATCH_SITE(ops, x)		\
  else if(type == paravirtState.ops##Offset + paravirtState.ops.memberOffset("" #x )) \
  {                                                         \
      ret = paravirt_patch_insns(ibuf, len, start_##ops##_##x, end_##ops##_##x);    \
  } 

    if(false){}
    PATCH_SITE(pv_irq_ops, restore_fl)
    PATCH_SITE(pv_irq_ops, save_fl)
    PATCH_SITE(pv_irq_ops, irq_enable)
    PATCH_SITE(pv_irq_ops, irq_disable)
    PATCH_SITE(pv_cpu_ops, iret)
    PATCH_SITE(pv_cpu_ops, irq_enable_sysexit)
    PATCH_SITE(pv_cpu_ops, usergs_sysret32)
    PATCH_SITE(pv_cpu_ops, usergs_sysret64)
    PATCH_SITE(pv_cpu_ops, swapgs)
    PATCH_SITE(pv_mmu_ops, read_cr2)
    PATCH_SITE(pv_mmu_ops, read_cr3)
    PATCH_SITE(pv_mmu_ops, write_cr3)
    PATCH_SITE(pv_cpu_ops, clts)
    PATCH_SITE(pv_mmu_ops, flush_tlb_single)
    PATCH_SITE(pv_cpu_ops, wbinvd)

    else
    {
        ret = paravirt_patch_default(type, clobbers, ibuf, addr, len);
    }
#undef PATCH_SITE
    return ret;
}

void ElfLoader::applyAltinstr(){
	uint64_t count = 0;
	uint64_t count_all = 0;
    uint8_t *instr;
    uint8_t *replacement;
    unsigned char insnbuf[255-1];

	SegmentInfo altinst = this->elffile->findSegmentWithName(".altinstructions");
	if (!altinst.index) return;

    SegmentInfo altinstreplace;
	altinstreplace = this->elffile->
			findSegmentWithName(".altinstr_replacement");

    struct alt_instr *start = (struct alt_instr*) altinst.index;
    struct alt_instr *end = (struct alt_instr*) (altinst.index + altinst.size);

	this->updateSegmentInfoMemAddress(altinstreplace);
	
	//Find boot_cpu_data in kernel
	Variable *boot_cpu_data_var = Variable::findVariableByName("boot_cpu_data");
	assert(boot_cpu_data_var);

	Instance boot_cpu_data = boot_cpu_data_var->getInstance();
    Instance x86_capability = boot_cpu_data.memberByName("x86_capability");

	uint32_t cpuCaps[10] = {0};
	for (uint8_t i = 0; i < 10; i++){
		cpuCaps[i] = x86_capability.arrayElem(i).getRawValue<uint32_t>(false);
	}

    for(struct alt_instr * a = start ; a < end ; a++)
	{
        //if (!boot_cpu_has(a->cpuid)) continue;

		count_all += 1;
		
        if (!((cpuCaps[a->cpuid / 32] >> (a->cpuid % 32)) & 0x1)){ 
			continue;
        }

		count += 1;

        instr = ((uint8_t *)&a->instr_offset) + a->instr_offset;
        replacement = ((uint8_t *)&a->repl_offset) + a->repl_offset;

		//If this is the a kernel then adjust the address of the instruction to replace
        if(dynamic_cast<ElfKernelLoader*>(this))
        {
            instr -= (uint64_t)(this->textSegment.index - 
					(uint64_t) this->elffile->getFileContent());
        }

        memcpy(insnbuf, replacement, a->replacementlen);

        // 0xe8 is a relative jump; fix the offset.
        if (insnbuf[0] == 0xe8 && a->replacementlen == 5)
        {
            //If replacement is in the altinstr_replace section fix the offset.
            if(replacement >= (uint8_t *)altinstreplace.index && 
				replacement < (uint8_t *)altinstreplace.index + altinstreplace.size)
            {
                *(int32_t *)(insnbuf + 1) -= (altinstreplace.index - this->textSegment.index) - (altinstreplace.memindex - this->textSegment.memindex);

            }
            *(int32_t *)(insnbuf + 1) += replacement - instr;
        }

        //add_nops
        add_nops(insnbuf + a->replacementlen, a->instrlen - a->replacementlen);

        memcpy(instr, insnbuf, a->instrlen);
    }

	std::cout << "Applied " << count << " / " << count_all << " Altinstructions" << std::endl;
}

void ElfLoader::applyParainstr(){
	uint64_t count = 0;
	SegmentInfo info = this->elffile->findSegmentWithName(".parainstructions");
	if (!info.index) return;
    
    //TODO add paravirt entries
    //bool addParavirtEntries = false;	
	//if(context.paravirtEntries.size() == 0) addParavirtEntries = true;
	
    struct paravirt_patch_site *start = (struct paravirt_patch_site *) info.index;
    struct paravirt_patch_site *end = (struct paravirt_patch_site *) (info.index + info.size);

    char insnbuf[254];

    //noreplace_paravirt is 0 in the kernel
    //http://lxr.free-electrons.com/source/arch/x86/kernel/alternative.c#L45
    //if (noreplace_paravirt) return;

    for (struct paravirt_patch_site *p = start; p < end; p++) {
        unsigned int used;

		count += 1;

        //BUG_ON(p->len > MAX_PATCH_LEN);
		//parainstructions: impossible length
        assert(p->len < 255);

		//TODO readd when needed
		//if(addParavirtEntries) {
		//    this->paravirtEntries.insert((uint64_t) p->instr);
		//}
		
        //p->instr points to text segment in memory
        //let it point to the address in the elf binary
        uint8_t * instrInElf = p->instr;
        instrInElf -= (uint64_t) this->textSegment.memindex;
        instrInElf += (uint64_t) this->textSegment.index;

        /* prep the buffer with the original instructions */
        memcpy(insnbuf, instrInElf, p->len);

        //p->instrtype is used as an offset to an array of pointers. 
		//Here we only use ist as Offset.
        used = paravirtNativePatch(p->instrtype * 8, p->clobbers, insnbuf,
                                   (unsigned long)p->instr, p->len);

		//"parainstructions: impossible length"
        assert(p->len < 255);

        /* Pad the rest with nops */
        add_nops(insnbuf + used, p->len - used);      //add_nops
        memcpy(instrInElf, insnbuf, p->len);   //memcpy
    }
	std::cout << "Applied " << count << " Paravirt instructions" << std::endl;
}

void ElfLoader::applySmpLocks(){
	SegmentInfo info = this->elffile->findSegmentWithName(".smp_locks");
	if (!info.index) return;
	this->updateSegmentInfoMemAddress(info);

    unsigned char lock = 0;
	uint64_t count = 0;
    
	int32_t * smpLocksStart = (int32_t *) info.index;
    int32_t * smpLocksStop  = (int32_t *) (info.index + info.size);
	
	//Find boot_cpu_data in kernel
	Variable *boot_cpu_data_var = Variable::findVariableByName("boot_cpu_data");
	assert(boot_cpu_data_var);
	
	Instance boot_cpu_data = boot_cpu_data_var->getInstance();
    Instance x86_capability = boot_cpu_data.memberByName("x86_capability");
    if (!((x86_capability.arrayElem(X86_FEATURE_UP / 32).
					getRawValue<uint32_t>(false) >> (X86_FEATURE_UP % 32)) & 0x1)){
        /* turn lock prefix into DS segment override prefix */
        lock = 0x3e;
    }else{
        /* turn DS segment override prefix into lock prefix */
        lock = 0xf0;
    }



    //bool addSmpEntries = false;
    //if(context.smpOffsets.size() == 0) addSmpEntries = true;
    
    for(int32_t * poff = smpLocksStart; poff < smpLocksStop ; poff++)
    {
		count += 1;
        uint8_t *ptr = (uint8_t *)poff + *poff;


        //Adapt offset in ELF
        int32_t offset = (info.index - this->textSegment.index) - 
			(info.memindex - this->textSegment.memindex);
        ptr -= offset;

        *ptr = lock;

        //if (addSmpEntries) context.smpOffsets.insert((quint64) ptr - (quint64) context.textSegment.index);
    }
	std::cout << "Applied " << count << " SMP instructions" << std::endl;
}

void ElfLoader::applyMcount(SegmentInfo &info){
    //See ftrace_init_module in kernel/trace/ftrace.c

	uint64_t count = 0;
    uint64_t * mcountStart = (uint64_t *) info.index;
    uint64_t * mcountStop  = (uint64_t *) (info.index + info.size);

    //bool addMcountEntries = false;
    //if(context.mcountEntries.size() == 0) addMcountEntries = true;
    for(uint64_t * i = mcountStart; i < mcountStop; i++)
    {
		count += 1;
        //if (addMcountEntries) context.mcountEntries.insert((*i));
        add_nops((void*) (this->textSegmentContent.data() + (*i) - this->textSegment.memindex), 5);
    }
	std::cout << "Applied " << count << " Mcount instructions" << std::endl;
}

void ElfLoader::applyJumpEntries(uint64_t jumpStart, uint32_t numberOfEntries){
	uint64_t count = 0;
	//Apply the jump tables after the segments are adjacent
    //jump_label_apply_nops() => http://lxr.free-electrons.com/source/arch/x86/kernel/module.c#L205
    //the entry type is 0 for disable and 1 for enable

    //bool addJumpEntries = false;
    //if(context.jumpEntries.size() == 0) addJumpEntries = true;

    //if(context.type == Detect::KERNEL_CODE)
    //{
    //    numberOfJumpEntries = (jumpStop - jumpStart) / sizeof(struct jump_entry);
    //}
    //else if(context.type == Detect::MODULE)
    //{
    //    numberOfJumpEntries = context.currentModule.member("num_jump_entries").toUInt32();
    //}

    struct jump_entry * startEntry = (struct jump_entry *) this->jumpTable.data();
    struct jump_entry * endEntry   = (struct jump_entry *) (this->jumpTable.data() + 
																this->jumpTable.size());

	BaseType* jump_entry_bt = BaseType::findBaseTypeByName("jump_entry");
	BaseType* static_key_bt = BaseType::findBaseTypeByName("static_key");
    for(uint32_t i = 0 ; i < numberOfEntries ; i++)
    {
        Instance jumpEntry = Instance(NULL, 0);
		if (dynamic_cast<ElfKernelLoader*>(this)){
			uint64_t instanceAddress = 0;
			
			//This is not a real array in memory but has more readability
			instanceAddress = (uint64_t) &((struct jump_entry *) jumpStart)[i];
			
			jumpEntry = jump_entry_bt->getInstance(instanceAddress);
            
			//Do not apply jump entries to .init.text
			

			uint64_t codeAddress = jumpEntry.memberByName("code").getValue<uint64_t>();
            if (codeAddress > 
					(uint64_t) this->textSegment.memindex + this->textSegment.size)
            {
                continue;
            }
		}
		else if (dynamic_cast<ElfModuleLoader*>(this)){
			//	TODO!!!!
			//    jumpEntry = context.currentModule.member("jump_entries").arrayElem(i);
		}

        uint64_t keyAddress = jumpEntry.memberByName("key").getValue<uint64_t>();

        //if(doPrint) Console::out() << hex << "Code: " << jumpEntry.member("code").toUInt64() << " target: " << jumpEntry.member("target").toUInt64() << dec << endl;
        //if(doPrint) Console::out() << hex << "Code offset : " << jumpEntry.member("code").toUInt64() - textSegmentInMem << " target offset : " << jumpEntry.member("target").toUInt64() - textSegmentInMem << dec << endl;

        Instance key = static_key_bt->getInstance(keyAddress);
        uint64_t enabled = key.memberByName("enabled")
							  .memberByName("counter")
							  .getValue<int64_t>();

        //if(doPrint) Console::out() << hex << "Key @ " << keyAddress << " is: " << enabled << dec << endl;

		uint64_t codeEntry = jumpEntry.memberByName("code").getValue<uint64_t>();
        for (struct jump_entry * entry = startEntry ; entry < endEntry; entry++){
            //Check if current elf entry is current kernel entry
            if (codeEntry ==  entry->code)
            {

				count +=1;
                uint64_t patchOffset = entry->code - 
								(uint64_t) this->textSegment.memindex;

                char * patchAddress = (char *) (patchOffset + 
								(uint64_t) this->textSegmentContent.data());

                //if(doPrint) Console::out() << "Jump Entry @ " << hex << patchOffset << dec;
                //if(doPrint) Console::out() << " " << ((enabled) ? "enabled" : "disabled") << endl;

                int32_t destination = entry->target - (entry->code + 5);
                //if(addJumpEntries){
                //    context.jumpEntries.insert(entry->code, destination);
                //    context.jumpDestinations.insert(entry->target);
                //}


                if(enabled)
                {
                    //if(doPrint) Console::out() << hex << "Patching jump " << 
					//    "@ : " << patchOffset << dec << endl;
                    *patchAddress = (char) 0xe9;
                    *((int32_t*) (patchAddress + 1)) = destination;
                }
                else
                {
                    add_nops(patchAddress, 5);      //add_nops
                }
            }
        }
    }
	std::cout << "Applied " << count << " JMP entries" << std::endl;
}

void ElfLoader::parseElfFile(){
	this->initText();
	this->initData();
}

KernelManager::KernelManager():
	dirName(), moduleMap(), moduleInstanceMap(), symbolMap(),
	moduleSymbolMap(), functionSymbolMap()
	{
}

void KernelManager::setKernelDir(std::string dirName){
	this->dirName = dirName;
}

ElfLoader *KernelManager::loadModule(std::string moduleName){
	if(moduleMap[moduleName] != NULL){
		return moduleMap[moduleName];
	}
	std::string filename = findModuleFile(moduleName);
	if(filename.empty()){
		std::cout << moduleName << ": Module File not found" << std::endl;
		return NULL;
	}else{
		//std::cout << filename << std::endl;
	}
	ElfFile *file = ElfFile::loadElfFile(filename);
	auto module = file->parseElf(ElfFile::ELFPROGRAMTYPEMODULE, moduleName, this);
	moduleMap[moduleName] = module;

	return module;
}


void KernelManager::loadAllModules(){
	std::list<std::string> moduleNames = this->getKernelModules();

	for (auto curStr : moduleNames ){
		this->loadModule(curStr);
	}
}

Instance KernelManager::nextModule(Instance &instance){
	Instance next = instance.memberByName("list").memberByName("next", true);
	next = next.changeBaseType("module");
	return next;
}

std::string KernelManager::findModuleFile(std::string modName){
	for( fs::recursive_directory_iterator end, dir(this->dirName);
			dir != end; dir++){
		if(fs::extension(*dir) == ".ko"){
			if((*dir).path().stem() == modName){
				return (*dir).path().native();
			}
			std::replace(modName.begin(), modName.end(), '_', '-');
			if((*dir).path().stem() == modName){
				return (*dir).path().native();
			}
			std::replace(modName.begin(), modName.end(), '-', '_');
			if((*dir).path().stem() == modName){
				return (*dir).path().native();
			}
		}
	}
	return "";

}

std::list<std::string> KernelManager::getKernelModules(){
	if(this->moduleInstanceMap.size() == 0){
		this->loadKernelModules();
	}
	std::list<std::string> strList;
	for(auto mod: this->moduleInstanceMap){
	    strList.push_back(mod.first);
	}
	return strList;
}

Instance KernelManager::getKernelModuleInstance(std::string modName){
	std::replace(modName.begin(), modName.end(), '-', '_');
	if(this->moduleInstanceMap.find(modName) != this->moduleInstanceMap.end()){
		return this->moduleInstanceMap[modName];
	}
	assert(false);
	return Instance();
}

void KernelManager::loadKernelModules(){
	moduleInstanceMap.clear();
	Instance modules = Variable::findVariableByName("modules")->getInstance();
	Instance module = modules.memberByName("next", true);
	modules = modules.changeBaseType("module");
	module = module.changeBaseType("module");
	
	while(module != modules){
		std::string moduleName = module.memberByName("name").getRawValue<std::string>();
		//std::cout << "Module " << moduleName << std::endl;
		moduleInstanceMap[moduleName] = module;
		module = this->nextModule(module);
	}
}

uint64_t KernelManager::getSystemMapAddress(std::string name){
	if(this->symbolMap.find(name) != this->symbolMap.end()){
		return this->symbolMap[name];
	}
	return 0;
}
void KernelManager::addSymbolAddress(std::string name, uint64_t address){
	this->moduleSymbolMap[name] = address;
}

uint64_t KernelManager::getSymbolAddress(std::string name){
	if(this->moduleSymbolMap.find(name) != this->moduleSymbolMap.end()){
		return this->moduleSymbolMap[name];
	}
	return 0;
}

void KernelManager::addFunctionAddress(std::string name, uint64_t address){
	this->functionSymbolMap[name] = address;
}

uint64_t KernelManager::getFunctionAddress(std::string name){
	if(this->functionSymbolMap.find(name) != this->functionSymbolMap.end()){
		return this->functionSymbolMap[name];
	}
	return 0;
}


void KernelManager::parseSystemMap(){
	std::string sysMapFileName = this->dirName;
	sysMapFileName.append("/System.map");
	std::string line;
	std::ifstream sysMapFile (sysMapFileName);
	if (sysMapFile.is_open()){
		while ( sysMapFile.good() ){
			uint64_t address;
			char mode = '\0';
			std::string varname;
			std::getline (sysMapFile,line);
			std::stringstream iss(line); 
			iss >> std::hex >>address >> mode >> varname;

			symbolMap[varname] = address;
		}
		sysMapFile.close();
    }else{  
		std::cout << "Unable to open file" << std::endl;
		return;
    }
}

ElfKernelLoader::ElfKernelLoader(ElfFile* elffile):
	ElfLoader(elffile),
	KernelManager(),
	vvarSegment(),
	dataNosaveSegment(),
	bssSegment(),
	rodataSegment(),
	fentryAddress(0),
	genericUnrolledAddress(0)
	{}

ElfKernelLoader::~ElfKernelLoader(){}

void ElfKernelLoader::initText(void) {

	ElfFile64* elffile = dynamic_cast<ElfFile64*>(this->elffile);

	this->textSegment = elffile->findSegmentWithName(".text");
	this->updateSegmentInfoMemAddress(this->textSegment);
	
	this->dataSegment = elffile->findSegmentWithName(".data");
	this->vvarSegment = elffile->findSegmentWithName(".vvar");
	this->dataNosaveSegment = elffile->findSegmentWithName(".data_nosave");
	this->bssSegment = elffile->findSegmentWithName(".bss");


	this->fentryAddress = this->elffile->findAddressOfVariable("__fentry__");
	this->genericUnrolledAddress = this->
			elffile->findAddressOfVariable("copy_user_generic_unrolled");

	applyAltinstr();
	applyParainstr();
	applySmpLocks();


	this->textSegmentContent.insert(this->textSegmentContent.end(),
			this->textSegment.index,
			this->textSegment.index + this->textSegment.size);


	SegmentInfo info = elffile->findSegmentWithName(".notes");
	uint64_t offset = (uint64_t) info.index - (uint64_t) this->textSegment.index;
	this->textSegmentContent.insert(this->textSegmentContent.end(),
			offset - this->textSegmentContent.size(), 0);
	this->textSegmentContent.insert(this->textSegmentContent.end(),
			info.index, info.index + info.size);

	info = elffile->findSegmentWithName("__ex_table");
	offset = (uint64_t) info.index - (uint64_t) this->textSegment.index;
	this->textSegmentContent.insert(this->textSegmentContent.end(),
				offset - this->textSegmentContent.size(), 0);
	this->textSegmentContent.insert(this->textSegmentContent.end(),
			info.index, info.index + info.size);


	//Apply Ftrace changes
	info = elffile->findSegmentWithName(".init.text");
	uint64_t initTextOffset = -(uint64_t) info.address + (uint64_t) info.index;

	info.index = (uint8_t *) elffile->findAddressOfVariable("__start_mcount_loc") + initTextOffset;
	info.size = (uint8_t *) elffile->findAddressOfVariable("__stop_mcount_loc") + initTextOffset - info.index;
	applyMcount(info);

	//TODO! also enable this some time later
	//Apply Tracepoint changes
	//    SegmentInfo rodata = findElfSegmentWithName(fileContent, ".rodata");
	//    qint64 rodataOffset = - (quint64)rodata.address + (quint64)rodata.index;
	//    info.index = (char *)findElfAddressOfVariable(fileContent, context, "__start___tracepoints_ptrs") + rodataOffset;
	//    info.size = (char *)findElfAddressOfVariable(fileContent, context, "__stop___tracepoints_ptrs") + rodataOffset - info.index ;
	//    applyTracepoints(info, rodata, context, textSegmentContent);

	info = elffile->findSegmentWithName(".data");
	int64_t dataOffset = -(uint64_t) info.address + (uint64_t) info.index;
	uint64_t jumpStart = elffile->findAddressOfVariable("__start___jump_table");
	uint64_t jumpStop = elffile->findAddressOfVariable("__stop___jump_table");

	info.index = (uint8_t *) jumpStart + dataOffset;
	info.size = (uint8_t *) jumpStop + dataOffset - info.index;

	//Save the jump_labels section for later reference.
	if (info.index != 0){
		this->jumpTable.insert(this->jumpTable.end(),
					info.index, info.index + info.size);
	}
    uint32_t numberOfEntries = (jumpStop - jumpStart) / sizeof(struct jump_entry);


	applyJumpEntries( jumpStart, numberOfEntries );
}

//TODO the following must also be put in its own function
//
//	// Hash
//	QCryptographicHash hash(QCryptographicHash::Sha1);
//
//	for (int i = 0;
//			i <= context.textSegmentContent.size() / KERNEL_CODEPAGE_SIZE;
//			i++) {
//		PageData page = PageData();
//		hash.reset();
//		// Caclulate hash of one segment at the ith the offset
//		QByteArray segment = context.textSegmentContent.mid(
//				i * KERNEL_CODEPAGE_SIZE, KERNEL_CODEPAGE_SIZE);
//		if (!segment.isEmpty()) {
//			//Remember how long the contents of the text segment are,
//			//this is to identify the uninitialized data
//			if (segment.size() != KERNEL_CODEPAGE_SIZE) {
//				if ((segment.size() + 1) % PAGE_SIZE != 0) {
//					quint32 size = segment.size();
//					size += PAGE_SIZE - (size % PAGE_SIZE);
//					context.textSegmentInitialized = i * KERNEL_CODEPAGE_SIZE
//							+ size;
//				}
//			}
//			segment = segment.leftJustified(KERNEL_CODEPAGE_SIZE, 0);
//			page.content = segment;
//			hash.addData(page.content);
//			page.hash = hash.result();
//			context.textSegmentData.append(page);
//		}
//		//Console::out() << "The " << i << "th segment got a hash of: " << segmentHashes.last().toHex() << " Sections." << endl;
//	}
//
void ElfKernelLoader::initData(void){

//	//TODO
//	//.data
//	//.vvar
//	QByteArray vvarSegmentContent = QByteArray();
//	vvarSegmentContent.append(context.vvarSegment.index,
//			context.vvarSegment.size);
//	for (int i = 0; i <= vvarSegmentContent.size() / 0x1000; i++) {
//		PageData page = PageData();
//		hash.reset();
//		// Caclulate hash of one segment at the ith the offset
//		QByteArray segment = vvarSegmentContent.mid(i * 0x1000, 0x1000);
//		if (!segment.isEmpty()) {
//			segment = segment.leftJustified(0x1000, 0);
//			page.content = segment;
//			hash.addData(page.content);
//			page.hash = hash.result();
//			context.vvarSegmentData.append(page);
//		}
//	}
//	//.data_nosave
//	QByteArray dataNosaveSegmentContent = QByteArray();
//	dataNosaveSegmentContent.append(context.vvarSegment.index,
//			context.vvarSegment.size);
//	for (int i = 0; i <= dataNosaveSegmentContent.size() / 0x1000; i++) {
//		PageData page = PageData();
//		hash.reset();
//		// Caclulate hash of one segment at the ith the offset
//		QByteArray segment = dataNosaveSegmentContent.mid(i * 0x1000, 0x1000);
//		if (!segment.isEmpty()) {
//			segment = segment.leftJustified(0x1000, 0);
//			page.content = segment;
//			hash.addData(page.content);
//			page.hash = hash.result();
//			context.dataNosaveSegmentData.append(page);
//		}
//	}
//	//.bss
//
}
//
//	//Initialize the symTable in the context for later reference
//	if (fileContent[4] == ELFCLASS32) {
//		//TODO
//	} else if (fileContent[4] == ELFCLASS64) {
//		Elf64_Ehdr * elf64Ehdr = (Elf64_Ehdr *) fileContent;
//		Elf64_Shdr * elf64Shdr = (Elf64_Shdr *) (fileContent
//				+ elf64Ehdr->e_shoff);
//
//		quint32 symSize = elf64Shdr[context.symindex].sh_size;
//		Elf64_Sym *symBase = (Elf64_Sym *) (fileContent
//				+ elf64Shdr[context.symindex].sh_offset);
//
//		for (Elf64_Sym * sym = symBase;
//				sym < (Elf64_Sym *) (((char*) symBase) + symSize); sym++) {
//			//We also need to know about private functions for data verification, so also save them here.
//			//TODO fix scope
//			if (ELF64_ST_TYPE(sym->st_info) & (STT_FUNC)
//					|| (ELF64_ST_TYPE(sym->st_info) == (STT_NOTYPE)))
//					//if(ELF64_ST_TYPE(sym->st_info) & (STT_FUNC) || (ELF64_ST_TYPE(sym->st_info) == (STT_NOTYPE) && ELF64_ST_BIND(sym->st_info) & STB_GLOBAL))
//					{
//				QString symbolName =
//						QString(
//								&((fileContent
//										+ elf64Shdr[context.strindex].sh_offset)[sym->st_name]));
//				quint64 symbolAddress = sym->st_value;
//				_funcTable.insert(symbolName, symbolAddress);
//			}
//			if (ELF64_ST_BIND(sym->st_info) & STB_GLOBAL) {
//				QString symbolName =
//						QString(
//								&((fileContent
//										+ elf64Shdr[context.strindex].sh_offset)[sym->st_name]));
//				quint64 symbolAddress = sym->st_value;
//				if (!_symTable.contains(symbolName)) {
//					_symTable.insert(symbolName, symbolAddress);
//				}
//			}
//		}
//	}
//
//	return context;
// end of parseElfFile()

void ElfKernelLoader::updateSegmentInfoMemAddress(SegmentInfo &info){
	info.memindex = (uint8_t *) info.address;
}

////////////////////////////////////////////////////

ElfModuleLoader::ElfModuleLoader(ElfFile* elffile, 
	    std::string name,
		KernelManager* parent):
	ElfLoader(elffile),
	modName(name),
	parent(parent){
}

ElfModuleLoader::~ElfModuleLoader(){}

void ElfModuleLoader::loadDependencies(void) {
	SegmentInfo miS = elffile->findSegmentWithName(".modinfo");

	//parse .modinfo and load dependencies
	char *modinfo = (char*) miS.index;
	char *module = NULL;
	if(!modinfo) return;

	while (modinfo < (char*) (miS.index) + miS.size)
	{
		//std::cout << "Searching for string" << std::endl;
		//check if the string starts with depends
		if(modinfo[0] == 0){
			modinfo++;
			continue;
		}else if(strncmp(modinfo, "depends", 7) != 0){
			modinfo += strlen(modinfo) + 1;
			continue;
		}else{
			//string.compare(0, 7, "depends")
			modinfo += 8;
			
			module = strtok(modinfo, ",");
			while(module != NULL){
				if(*module == 0) break;
				parent->loadModule(module);
				module = strtok(NULL, ",");
			}
				
			return;
		}
	}

}

void ElfModuleLoader64::applyRelocationsOnSection(uint32_t relSectionID){
	ElfFile64 *elf= dynamic_cast<ElfFile64 *>(this->elffile);
	assert(elf);

	SegmentInfo relSectionInfo = 
		        this->elffile->findSegmentByID(relSectionID);
	
    Elf32_Word sectionID = elf->elf64Shdr[relSectionID].sh_info;
	std::string sectionName = this->elffile->segmentName(sectionID);

	SegmentInfo sectionInfo = 
		        this->elffile->findSegmentByID(sectionID);
	this->updateSegmentInfoMemAddress(sectionInfo);
	Elf64_Rela *rel = (Elf64_Rela *) relSectionInfo.index;

    Elf64_Sym *symBase = (Elf64_Sym *) this->elffile->
	                     segmentAddress(this->elffile->symindex);

	SegmentInfo percpuDataSegment = 
	    this->elffile->findSegmentWithName(".data..percpu");
	Instance currentModule = this->parent->
	                               getKernelModuleInstance(this->modName);
	


#ifdef PRINTDEBUG
    bool doPrint = false;
    if(sectionName.compare("__kcrctab_gpl") == 0) doPrint = true;
    if(doPrint) Console::out() << "SectioN To Relocate: " << sectionName << dec << endl;
#endif
	
	SegmentInfo symRelSectionInfo;
	
    for (uint32_t i = 0; i < relSectionInfo.size / sizeof(*rel); i++) {
		void *locInElf = 0;
		void *locInMem = 0;
		void *locOfRelSectionInMem = 0;
		void *locOfRelSectionInElf = 0;

        /* This is where to make the change */
        locInElf = (void *) ((char*) sectionInfo.index    + rel[i].r_offset);
        locInMem = (void *) ((char*) sectionInfo.memindex + rel[i].r_offset);

		Elf64_Sym *sym = 0; 
        sym = symBase + ELF64_R_SYM(rel[i].r_info);

        switch(sym->st_shndx){
        case SHN_COMMON:

#ifdef PRINTDEBUG
            if(doPrint) Console::out() << "Symtype SHN_UNDEF" << endl;
            debugerr("This should not happen!");
#endif
			assert(false);
            continue; //TODO REMOVE
            break;
        case SHN_ABS:
#ifdef PRINTDEBUG
            if(doPrint) Console::out() << "Symtype SHN_ABS" << endl;
#endif
            break;
        case SHN_UNDEF:
#ifdef PRINTDEBUG
            if(doPrint) Console::out() << "Symtype SHN_UNDEF" << endl;
#endif
			sym->st_value = this->relocateShnUndef(
			                      this->elffile->symbolName(sym->st_name));
            break;
        default:
#ifdef PRINTDEBUG
            if(doPrint) Console::out() << "default: " << endl;
            //debugerr("Sym Type: default: " << sym->st_shndx);
#endif

            //TODO this is not right yet.
            /* Divert to percpu allocation if a percpu var. */
            if (sym->st_shndx == percpuDataSegment.segID){
                locOfRelSectionInMem = (void *) currentModule.
				                                    memberByName("percpu").
				                                    getValue<uint64_t>();
            }
            else
            {
				if (symRelSectionInfo.segID != sym->st_shndx){
					symRelSectionInfo = 
					    this->elffile->findSegmentByID(sym->st_shndx);
					this->updateSegmentInfoMemAddress(symRelSectionInfo);
				}
                locOfRelSectionInElf = (void *) symRelSectionInfo.index;
                locOfRelSectionInMem = (void *) symRelSectionInfo.memindex;
            }

            if(sym->st_value < (long unsigned int) locOfRelSectionInMem){
                sym->st_value += (long unsigned int) locOfRelSectionInMem;
            }
            break;
        }

        uint64_t val = sym->st_value + rel[i].r_addend;

#if PRINTDEBUG
		if(doPrint) Console::out() << "raddend: " << hex << rel[i].r_addend << dec << endl;
        if(doPrint) Console::out() << "sym->value: " << hex << sym->st_value << dec << endl;
        if(doPrint) Console::out() << "val: " << hex << val << dec << endl;
#endif

        switch (ELF64_R_TYPE(rel[i].r_info)) {
        case R_X86_64_NONE:
            break;
        case R_X86_64_64:
            *(uint64_t *)locInElf = val;
            break;
        case R_X86_64_32:
            *(uint64_t *)locInElf = val;
            if (val != *(uint64_t *)locInElf)
				assert(false);
			    return;
                //goto overflow;
            break;
        case R_X86_64_32S:
            *(uint32_t *)locInElf = val;
            if ((int64_t)val != *(int32_t *)locInElf)
				assert(false);
			    return;
                //goto overflow;
            break;
        case R_X86_64_PC32:

            // This line is from the original source the loc here is the
			//  location within the loaded module.

            //val -= (u64)loc;
            if(sectionName.compare(".altinstructions") == 0)
            {
                //This is later used to copy some memory
                val = val - (uint64_t)locOfRelSectionInMem + 
				            (uint64_t)locOfRelSectionInElf - 
				            (uint64_t)locInElf;
            }
            else
            {
                //This is used as relocation in memory
                val -= (uint64_t)locInMem;
            }
#ifdef PRINTDEBUG
            if(doPrint) Console::out() << "PC32 final value: " << hex << (quint32) val << dec << endl;
#endif
            *(uint32_t *)locInElf = val;
#if 0
            if ((int64_t)val != *(int32_t *)loc)
				assert(false);
			    return;
                //goto overflow;
#endif
            break;
        default:
#ifdef PRINTDEBUG
            debugerr("Unknown rela relocation: " << ELF64_R_TYPE(rel[i].r_info));
#endif
			assert(false);
		    return;
        }
#ifdef PRINTDEBUG
        doPrint = false;
#endif
    }
    return;

//#if 0
//overflow:
//    Console::err() << "overflow in relocation type " << (int)ELF64_R_TYPE(rel[i].r_info) << " val " << hex << val << endl;
//    Console::err() << "likely not compiled with -mcmodel=kernel" << endl;
//    return -ENOEXEC;
//#endif

}
uint64_t ElfModuleLoader64::relocateShnUndef(std::string symbolName){

	// First look into the system map.
	// As we depend on dwarf anyway we use that information to find
	// a variable.
	
	uint64_t address = this->parent->getSystemMapAddress(symbolName);
    if(address != 0){
        return address;
    }
	address = this->parent->getSymbolAddress(symbolName);
    if(address != 0){
        return address;
    }
	address = this->parent->getFunctionAddress(symbolName);
    if(address != 0){
        return address;
    }
#if 0
	// Assume we already have the correct object ...
	// Thus the following is not necessary
	// If it is, this section filteres weak objects
	
    else if (_sym.memSpecs().systemMap.count(symbolName) > 0)
    {
		//Try to find variable in system map
        //Console::out() << "Found Variable in system.map: " << symbolName) << endl;
        //sym->st_value = _sym.memSpecs().systemMap.value(symbolName).address;
        QList<SystemMapEntry> symbols = _sym.memSpecs().systemMap.values(symbolName);
        for (QList<SystemMapEntry>::iterator i = symbols.begin(); i != symbols.end(); ++i)
        {
            SystemMapEntry currentEntry = (*i);

            //ELF64_ST_BIND(sym->st_info) => 0: Local, 1: Global, 2: Weak
            //currentEntry.type => 'ascii' lowercase: local, uppercase: global
            if (ELF64_ST_BIND(sym->st_info) == 1 && currentEntry.type >= 0x41 && currentEntry.type <= 0x5a)
            {
                if(doPrint) Console::out() << "Symbol found in System Map: " << hex << currentEntry.address << " With type: Global" << dec << endl;
                sym->st_value = currentEntry.address;
            }
            else if (ELF64_ST_BIND(sym->st_info) == 0 && currentEntry.type >= 0x61 && currentEntry.type <= 0x7a)
            {
                if(doPrint) Console::out() << "Symbol found in System Map: " << hex << currentEntry.address << " With type: Local" << dec << endl;
                sym->st_value = currentEntry.address;
            }
        }
    }
#endif

	// Variable not found in system.map
    // Try to find the variable by name in insight.
    Function *func = Function::findFunctionByName(symbolName);
    if (func){
		return func->getAddress();
    }

	Variable *var = Variable::findVariableByName(symbolName);
    if (var){
		return var->getLocation();
	}
	assert(false);
	return 0;
}

void ElfModuleLoader::initText(void) {

	std::cout << "Loading module " << this->modName << std::endl;

	this->loadDependencies();

	this->elffile->applyRelocations(this);
	
	this->textSegment = this->elffile->findSegmentWithName(".text");
	this->updateSegmentInfoMemAddress(this->textSegment);
	this->dataSegment = this->elffile->findSegmentWithName(".data");


	applyAltinstr();
	applyParainstr();
	applySmpLocks();

    //Content of text section in memory:
    //same as the sections in the elf binary

    this->textSegmentContent.clear();
	this->textSegmentContent.insert(this->textSegmentContent.end(),
			this->textSegment.index,
			this->textSegment.index + this->textSegment.size);

	uint8_t *fileContent = this->elffile->getFileContent();
    Elf64_Ehdr * elf64Ehdr = (Elf64_Ehdr *) fileContent;
    Elf64_Shdr * elf64Shdr = (Elf64_Shdr *) (fileContent + elf64Ehdr->e_shoff);
    for(unsigned int i = 0; i < elf64Ehdr->e_shnum; i++)
    {
		std::string sectionName = this->elffile->segmentName(i);
        if (sectionName.compare(".text") == 0 ||
            sectionName.compare(".init.text") == 0){
			continue;
		}

        if(elf64Shdr[i].sh_flags == (SHF_ALLOC | SHF_EXECINSTR)){
			this->textSegmentContent.insert(this->textSegmentContent.end(),
			      fileContent + elf64Shdr[i].sh_offset, 
			      fileContent + elf64Shdr[i].sh_offset + elf64Shdr[i].sh_size);
        }
    }

	//TODO resume here

//    //Save the jump_labels section for later reference.
//
//    info = findElfSegmentWithName(fileContent, "__jump_table");
//    if(info.index != 0) context.jumpTable.append(info.index, info.size);
//
//    updateKernelModule(context);

    //Initialize the symTable in the context for later reference
	this->addSymbols();

//    context.rodataSegment = this->findElfSegmentWithName(context.fileContent, QString(".note.gnu.build-id"));
//    context.rodataSegment.address = (this->findMemAddressOfSegment(context, QString(".note.gnu.build-id")));
//
//    context.rodataContent.clear();
//    
//    // Populate rodata
//    Elf64_Ehdr * elf64Ehdr = (Elf64_Ehdr *) fileContent;
//    Elf64_Shdr * elf64Shdr = (Elf64_Shdr *) (fileContent + elf64Ehdr->e_shoff);
//    for(unsigned int i = 0; i < elf64Ehdr->e_shnum; i++)
//    {
//        if(((elf64Shdr[i].sh_flags == SHF_ALLOC  || elf64Shdr[i].sh_flags == (uint64_t) 0x32) &&
//                ( elf64Shdr[i].sh_type == SHT_PROGBITS )) ||
//             (elf64Shdr[i].sh_flags == SHF_ALLOC && elf64Shdr[i].sh_type == SHT_NOTE))
//        {
//            QString sectionName = QString(fileContent + elf64Shdr[elf64Ehdr->e_shstrndx].sh_offset + elf64Shdr[i].sh_name);
//            if(sectionName.compare(QString(".modinfo")) == 0 ||
//                   sectionName.compare(QString("__versions")) == 0 ||
//                   sectionName.startsWith(".init") ) continue;
//            uint64_t align = (elf64Shdr[i].sh_addralign ?: 1) - 1;
//            uint64_t alignmentSize = (context.rodataContent.size() + align) & ~align;
//            context.rodataContent = context.rodataContent.leftJustified(alignmentSize, 0);
//            context.rodataContent.append(fileContent + elf64Shdr[i].sh_offset, elf64Shdr[i].sh_size);
//
////            std::cout << hex << "Adding Section "
////                           << context.currentModule.member("name").toString() << " / " << sectionName
////                           << " Align: " << alignmentSize << " Size: " << elf64Shdr[i].sh_size
////                           << dec << std::endl;
//        }
//    }
//
//    //writeModuleToFile(fileName, currentModule, fileContent );
//    return context;

}

void ElfModuleLoader64::addSymbols(){

    SegmentInfo symInfo = this->elffile->findSegmentByID(this->elffile->symindex);

    uint32_t symSize = symInfo.size;
    Elf64_Sym *symBase = (Elf64_Sym *) symInfo.index;

    for(Elf64_Sym * sym = symBase; 
	    sym < (Elf64_Sym *) (((char*) symBase) + symSize) ; 
	    sym++){

		if (sym->st_name == 0){
			continue;
		}
        
        std::string symbolName = this->elffile->symbolName(sym->st_name);

		if((ELF64_ST_TYPE(sym->st_info) & (STT_OBJECT | STT_FUNC)) && 
		    ELF64_ST_BIND(sym->st_info) & STB_GLOBAL )
        {
            uint64_t symbolAddress = sym->st_value;

            // TODO update symtable
			this->parent->addSymbolAddress(symbolName, symbolAddress);
        }

        //We also have to consider local functions
        //if((ELF64_ST_TYPE(sym->st_info) & STT_FUNC) && ELF64_ST_BIND(sym->st_info) & STB_GLOBAL)
        if((ELF64_ST_TYPE(sym->st_info) == STT_FUNC))
        {
            if(symbolName.compare("") == 0) continue;
            if (ELF64_ST_BIND(sym->st_info) == STB_LOCAL){
			    //Store local variables with uniq names
                symbolName.append("_").append(this->modName);
				std::string newSymName = symbolName;
                //int i = 2;
                //while (_funcTable.contains(newSymName)){
                //    newSymName = symbolName;
                //    newSymName.append("_").append(i);
                //}
                symbolName = newSymName;
            }
            uint64_t symbolAddress = sym->st_value;
            if(symbolAddress < (uint64_t) this->textSegment.memindex){
                symbolAddress += (uint64_t) this->textSegment.memindex;
            }
			this->parent->addFunctionAddress(symbolName, symbolAddress);
        }
    }

}

void ElfModuleLoader::initData(void) {}

uint8_t *ElfModuleLoader::findMemAddressOfSegment(std::string segName){
	Instance module;
	Instance currentModule = this->parent->
	                               getKernelModuleInstance(this->modName);
	
	//If the searching for the .bss section
    //This section is right after the modules struct
	if(segName.compare(".bss") == 0){
        return (uint8_t *) currentModule.getAddress() + currentModule.size();
	}
	
	if(segName.compare("__ksymtab_gpl") == 0){
        return (uint8_t *) currentModule.memberByName("gpl_syms").
		                                 getRawValue<uint64_t>();
	}
	
	//Find the address of the current section in the memory image
    //Get Number of sections in kernel image
    Instance attrs = currentModule.memberByName("sect_attrs", true);
    uint32_t attr_cnt = attrs.memberByName("nsections").getValue<uint64_t>();

    //Now compare all section names until we find the correct section.
    for (uint j = 0; j < attr_cnt; ++j) {
        Instance attr = attrs.memberByName("attrs").arrayElem(j);
		std::string sectionName = attr.memberByName("name", true).
		                               getValue<std::string>();
		if(sectionName.compare(segName) == 0){
            return (uint8_t *) attr.memberByName("address").getValue<uint64_t>();
        }
    }
	//Segment not found
	assert(false);
    return 0;
}

void ElfModuleLoader::updateSegmentInfoMemAddress(SegmentInfo &info){
	info.memindex = this->findMemAddressOfSegment(info.segName);
}

////////////////////////////////////////////////////

ElfKernelLoader32::ElfKernelLoader32(ElfFile32* elffile):
	ElfKernelLoader(elffile){
	//this->ParseElfFile();
}

ElfKernelLoader32::~ElfKernelLoader32(){}

////////////////////////////////////////////////////

ElfKernelLoader64::ElfKernelLoader64(ElfFile64* elffile): 
	ElfKernelLoader(elffile){
	this->parseElfFile();
}

ElfKernelLoader64::~ElfKernelLoader64(){}

ElfModuleLoader32::ElfModuleLoader32(ElfFile32* elffile, 
		                             std::string name,
                                     KernelManager* parent):
	ElfModuleLoader(elffile, name, parent){
	//this->ParseElfFile();
}

ElfModuleLoader32::~ElfModuleLoader32(){}

////////////////////////////////////////////////////

ElfModuleLoader64::ElfModuleLoader64(ElfFile64* elffile, 
		                             std::string name,
                                     KernelManager* parent):
	ElfModuleLoader(elffile, name, parent){
	this->parseElfFile();
}

ElfModuleLoader64::~ElfModuleLoader64(){}
