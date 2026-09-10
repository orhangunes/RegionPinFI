#include "faultinjection.h"
#include <string.h>
#include <assert.h>
#include "pin.H"
#include "fi_cjmp_map.h"
#include "utils.h"
#include "instselector.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>

//#define INCLUDEALLINST
#define NOBRANCHES //always set
//#define NOSTACKFRAMEOP
//#define ONLYFP

UINT64 fi_inject_instance = 0;
UINT64 fi_iterator = 0;
UINT64 total_num_inst = 0;
int activated = 0;

CJmpMap jmp_map;

struct Range {
    UINT64 start;
    UINT64 end;
};

VOID FI_InjectFault_FlagReg(VOID * ip, UINT32 reg_num, UINT32 jmp_num, CONTEXT * ctxt) {

    if (fi_iterator == fi_inject_instance) {

        bool isvalid = false;
        const REG reg = reg_map.findInjectReg(reg_num);
        if (REG_valid(reg)) {
            isvalid = true;

            CJmpMap::JmpType jmptype = jmp_map.findJmpType(jmp_num);
            PRINT_MESSAGE(3, ("EXECUTING flag reg: Original Reg name %s value %p\n", REG_StringShort(reg).c_str(),
                (VOID * ) PIN_GetContextReg(ctxt, reg)));
            if (jmptype == CJmpMap::DEFAULT) {
                ADDRINT temp = PIN_GetContextReg(ctxt, reg);
                UINT32 inject_bit = jmp_map.findInjectBit(jmp_num);
                temp = temp ^ (1UL << inject_bit);
                PIN_SetContextReg(ctxt, reg, temp);
            } else if (jmptype == CJmpMap::USPECJMP) {
                ADDRINT temp = PIN_GetContextReg(ctxt, reg);
                UINT32 CF_val = (temp & (1UL << CF_BIT)) >> CF_BIT;
                UINT32 ZF_val = (temp & (1UL << ZF_BIT)) >> ZF_BIT;
                if (CF_val || ZF_val) {
                    temp = temp & (~(1UL << CF_BIT));
                    temp = temp & (~(1UL << ZF_BIT));
                } else {
                    temp = temp | (1UL << ZF_BIT);
                }
                PIN_SetContextReg(ctxt, reg, temp);
            } else {
                ADDRINT temp = PIN_GetContextReg(ctxt, reg);
                UINT32 SF_val = (temp & (1UL << SF_BIT)) >> SF_BIT;
                UINT32 OF_val = (temp & (1UL << OF_BIT)) >> OF_BIT;
                UINT32 ZF_val = (temp & (1UL << ZF_BIT)) >> ZF_BIT;
                if (ZF_val || (SF_val != OF_val)) {
                    temp = temp & (~(1UL << ZF_BIT));
                    if (SF_val != OF_val) {
                        temp = temp ^ (1UL << SF_BIT);
                    }
                } else {
                    temp = temp | (1UL << ZF_BIT);
                }
                PIN_SetContextReg(ctxt, reg, temp);
            }
            PRINT_MESSAGE(3, ("EXECUTING flag reg: Changed Reg name %s value %p\n", REG_StringShort(reg).c_str(),
                (VOID * ) PIN_GetContextReg(ctxt, reg)));
        }
        if (isvalid) {
            fprintf(activationFile, "Activated: Valid Reg name %s in %p\n", REG_StringShort(reg).c_str(), ip);
            fclose(activationFile); // can crash after this!
            activated = 1;
            fi_iterator++;
            PIN_ExecuteAt(ctxt);
        } else {
            fi_inject_instance++;
        }
    }
    fi_iterator++;
}

VOID inject_CCS(VOID * ip, UINT32 reg_num, CONTEXT * ctxt) {

    //need to consider FP regs and context
    if (fi_iterator == fi_inject_instance) {

        const REG reg = reg_map.findInjectReg(reg_num);
        int isvalid = 0;
        if (REG_valid(reg)) {
            isvalid = 1;

            if (reg_map.isFloatReg(reg_num)) {

                if (REG_is_xmm(reg)) {
                    PRINT_MESSAGE(4, ("Executing: xmm: Reg name %s\n", REG_StringShort(reg).c_str()));

                    FI_SetXMMContextReg(ctxt, reg, reg_num);
                } else if (REG_is_ymm(reg)) {

                    PRINT_MESSAGE(4, ("Executing: ymm: Reg name %s\n", REG_StringShort(reg).c_str()));

                    FI_SetYMMContextReg(ctxt, reg, reg_num);
                }
                else if (REG_is_fr(reg) || REG_is_mm(reg)) {
                    PRINT_MESSAGE(4, ("Executing: mm or x87: Reg name %s\n", REG_StringShort(reg).c_str()));

                    FI_SetSTContextReg(ctxt, reg, reg_num);
                } else {
                    fprintf(stderr, "Register %s not covered!\n", REG_StringShort(reg).c_str());
                    exit(3);
                }
            } else {

                ADDRINT temp = PIN_GetContextReg(ctxt, reg);
                srand((unsigned) time(0));
                UINT32 low_bound_bit = reg_map.findLowBoundBit(reg_num);
                UINT32 high_bound_bit = reg_map.findHighBoundBit(reg_num);

                UINT32 inject_bit = (rand() % (high_bound_bit - low_bound_bit)) + low_bound_bit;

                temp = (ADDRINT)(temp ^ (1UL << inject_bit));

                PIN_SetContextReg(ctxt, reg, temp);
            }

        }
        if (isvalid) {
            fprintf(activationFile, "Activated: Valid Reg name %s in %p\n", REG_StringShort(reg).c_str(), ip);
            fclose(activationFile); // can crash after this!
            activated = 1;
            fi_iterator++;

            PIN_ExecuteAt(ctxt);
        } else
            fi_inject_instance++;
    }
    fi_iterator++;
}


VOID FI_InjectFault_Mem(VOID * ip, VOID * memp, UINT32 size) {
    
    if (fi_iterator == fi_inject_instance) {

        UINT8 * temp_p = (UINT8 * ) memp;
        srand((unsigned) time(0));
        UINT32 inject_bit = rand() % (size * 8 /* bits in one byte*/ );

        UINT32 byte_num = inject_bit / 8;
        UINT32 offset_num = inject_bit % 8;

        *(temp_p + byte_num) = * (temp_p + byte_num) ^ (1U << offset_num);

        fprintf(activationFile, "Activated: Memory injection\n");
        fclose(activationFile); // can crash after this!
        activated = 1;

        fi_iterator++; //This is because the inject_reg will mistakenly add 1 more time when injecting 
    }

    fi_iterator++;
}

VOID FI_InjectFault_MEM_ECC(VOID * ip, VOID * memp, UINT32 size, UINT32 num, BOOL mode) {

    if (fi_iterator == fi_inject_instance) {

        fprintf(activationFile, "Executing %p, memory %p, value %lld, in hex %llx, size %d\n",
            ip, memp, *((long long * ) memp), ( * ((long long * ) memp)), size);

        UINT8 * temp_p = (UINT8 * ) memp;
        srand((unsigned) time(0));
        UINT32 last_inject_bit = 0;
        for (UINT32 i = 0; i < num; i++) {
            UINT32 inject_bit = rand() % (size * 8 /* bits in one byte*/ );

            if ((i == num - 1) && mode) {
                inject_bit = last_inject_bit + 1;
                if (inject_bit == size * 8)
                    inject_bit = 0; // just do this for now. This case should be rare.
            }
            UINT32 byte_num = inject_bit / 8;
            UINT32 offset_num = inject_bit % 8;

            *(temp_p + byte_num) = * (temp_p + byte_num) ^ (1U << offset_num);

            fprintf(activationFile, "Executing %p, memory %p, value %lld, in hex %llx, injected_bit %d\n",
                ip, memp, *((long long * ) memp), ( * ((long long * ) memp)), inject_bit);
            last_inject_bit = inject_bit;
        }
        fprintf(activationFile, "Activated: Memory injection\n");
        fclose(activationFile); // can crash after this!
        activated = 1;

        fi_iterator++; //This is because the inject_reg will mistakenly add 1 more time when injecting
    }

    fi_iterator++;
}

VOID instruction_InstrumentationECC(INS ins, VOID * v) {
    int num = multibits.Value();
    bool mode = consecutive.Value();
    if (!isValidInst(ins))
        return;

    if (INS_IsMemoryRead(ins)) {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR) FI_InjectFault_MEM_ECC,
            IARG_ADDRINT, INS_Address(ins),
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_UINT32, num,
            IARG_BOOL, mode,
            IARG_END);
    }

}

VOID instruction_Instrumentation(INS ins, VOID * v) {
    // decides where to insert the injection calls and what calls to inject
    if (!isValidInst(ins))
        return;

    int numW = INS_MaxNumWRegs(ins), randW = 0;
    UINT32 index = 0;
    REG reg;

    #ifdef INCLUDEALLINST
    int mayChangeControlFlow = 0;
    if (!INS_HasFallThrough(ins))
        mayChangeControlFlow = 1;
    for (int i = 0; i < numW; i++) {
        reg = INS_RegW(ins, i);
        if (reg == REG_RIP || reg == REG_EIP || reg == REG_IP) // conditional branches
        {
            mayChangeControlFlow = 1;
            break;
        }
    }
    if (numW > 1)
        randW = random() % numW;
    if (numW > 1 && (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS))
        randW = (randW + 1) % numW;
    if (numW > 1 && REG_valid(INS_RegW(ins, randW)))
        reg = INS_RegW(ins, randW);
    else
        reg = INS_RegW(ins, 0);
    if (!REG_valid(reg))
        return;
    index = reg_map.findRegIndex(reg);
    LOG("ins:" + INS_Disassemble(ins) + "\n");
    LOG("reg:" + REG_StringShort(reg) + "\n");

    // FIXME: INCLUDEINST is not used now. However, if you enable this option
    // in the future, you need to change the code below. If it changes the 
    // control flow, you need to inject fault in the read register rather than
    // write register
    if (mayChangeControlFlow)
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR) inject_CCS,
            IARG_ADDRINT, INS_Address(ins),
            IARG_UINT32, index,
            IARG_CONTEXT,
            IARG_END);
    else
        INS_InsertPredicatedCall(
            ins, IPOINT_AFTER, (AFUNPTR) inject_CCS,
            IARG_ADDRINT, INS_Address(ins),
            IARG_UINT32, index,
            IARG_CONTEXT,
            IARG_END);
    #else

    #ifdef NOBRANCHES
    if (INS_IsBranch(ins) || !INS_HasFallThrough(ins)) {
        //LOG("faultinject: branch/ret inst: " + INS_Disassemble(ins) + "\n");
        return;
    }
    #endif

    // NOSTACKFRAMEOP must be used together with NOBRANCHES, IsStackWrite 
    // has a bug that does not put pop into the list
    #ifdef NOSTACKFRAMEOP
    if (INS_IsStackWrite(ins) || OPCODE_StringShort(INS_Opcode(ins)) == "POP") {
        //LOG("faultinject: stack frame change inst: " + INS_Disassemble(ins) + "\n");    
        return;
    }
    #endif

    #ifdef ONLYFP
    bool hasfp = false;
    for (int i = 0; i < numW; i++) {
        if (reg_map.isFloatReg(reg)) {
            hasfp = true;
            break;
        }
    }
    if (!hasfp) {
        return;
    }
    #endif

    // select instruction based on instruction type
    if (!isInstFITarget(ins))
        return;

    if (numW > 1)
        randW = random() % numW;
    else
        randW = 0;

    // Jiesheng
    reg = INS_RegW(ins, randW);
    #ifdef ONLYFP
    while (!reg_map.isFloatReg(reg)) {
        randW = (randW + 1) % numW;
        reg = INS_RegW(ins, randW);
    }
    #endif

    if (numW > 1 && (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS))
        randW = (randW + 1) % numW;
    
    if (numW > 1 && REG_valid(INS_RegW(ins, randW)))
        reg = INS_RegW(ins, randW);
    else
        reg = INS_RegW(ins, 0);
    
    if (!REG_valid(reg)) {

        LOG("REGNOTVALID: inst " + INS_Disassemble(ins) + "\n");
        return;
    }
    index = reg_map.findRegIndex(reg);
    LOG("ins:" + INS_Disassemble(ins) + "\n");
    LOG("reg:" + REG_StringShort(reg) + "\n");

    // Jiesheng Wei
    if (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS) {
        INS next_ins = INS_Next(ins);
        if (INS_Valid(next_ins) && INS_Category(next_ins) == XED_CATEGORY_COND_BR) {
            //LOG("inject flag bit:" + REG_StringShort(reg) + "\n");

            UINT32 jmpindex = jmp_map.findJmpIndex(OPCODE_StringShort(INS_Opcode(next_ins)));
            INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR) FI_InjectFault_FlagReg,
                IARG_INST_PTR,
                IARG_UINT32, index,
                IARG_UINT32, jmpindex,
                IARG_CONTEXT,
                IARG_END);
            return;
        } else if (INS_IsMemoryWrite(ins)) {
            LOG("COMP2MEM: inst " + INS_Disassemble(ins) + "\n");

            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR) FI_InjectFault_Mem,
                IARG_ADDRINT, INS_Address(ins),
                IARG_MEMORYREAD_EA,
                IARG_MEMORYREAD_SIZE,
                IARG_END);
            return;

        } else {
            LOG("NORMAL FLAG REG: inst " + INS_Disassemble(ins) + "\n");
        }

    }

    INS_InsertPredicatedCall(
        ins, IPOINT_AFTER, (AFUNPTR) inject_CCS,
        IARG_ADDRINT, INS_Address(ins),
        IARG_UINT32, index,
        IARG_CONTEXT,
        IARG_END);
    #endif

}

/**
 * ORHAN COMMENT
 * This code block reads the instruction counts from a given file (pin.instcount_" + func_name + ".txt)
 * and updates global variables such as fi_inject_instance and total_num_inst.
 *  */
VOID get_instance_number(const char * fi_instcount_file) {
    /** ATTENTION!!! This file is used by the pintool, FI is not performed without reading it here.
     * The active reg is kept in the file. For example: Activated: Valid Reg name rsi in 0x558d7966c5bf
     */
    activationFile = fopen(fi_activation_file.Value().c_str(), "a");

    std::ifstream infile(GetInstCountFileName().c_str());
    std::string line;
    std::vector < Range > instruction_ranges;

    if (infile.is_open()) {
        // Read the first line and parse the total number of instructions
        std::getline(infile, line);
        std::istringstream iss(line);
        std::string temp;
        iss >> temp >> temp >> total_num_inst;

        // Read instruction ranges
        while (std::getline(infile, line)) {
            if (line.find("[") != std::string::npos) {
                UINT64 start, end;
                sscanf(line.c_str(), " [%lu, %lu]", & start, & end);
                instruction_ranges.push_back({
                    start,
                    end
                });
            }
        }
        infile.close();
    } else {
        PRINTF("\n*** Error: Instcount file reading error!!! => %s file not found \n\n", GetInstCountFileName().c_str())
        exit(-1);
    }

    if (!(fioption.Value() == CCS_INST || fioption.Value() == FP_INST || fioption.Value() == SP_INST || fioption.Value() == ALL_INST)) {
        fprintf(stderr, "ERROR, Specify one of valid options\n");
        exit(1);
    }

    PRINTF("\n*** Total Number of Instructions: %lu", total_num_inst);

    // Select a random range and pick a random instruction count within this range
    std::srand(std::time(nullptr));
    int random_index = std::rand() % instruction_ranges.size();
    Range selected_range = instruction_ranges[random_index];

    fi_inject_instance = selected_range.start + std::rand() % (selected_range.end - selected_range.start + 1);

    PRINTF("\n*** Randomly Selected Instruction Range: [%lu,%lu]", selected_range.start, selected_range.end);
    PRINTF("\n*** Randomly Selected Instruction Count: %lu \n\n", fi_inject_instance);
}

VOID Fini(INT32 code, VOID * v) {
    if (!activated) {
        fprintf(activationFile, "Not Activated!\n");
        fclose(activationFile);
    }
}

/*ORHAN COMMENT*/
VOID functionEntry(ADDRINT addr) {

    PIN_LockClient();

    RTN rtn = RTN_FindByAddress(addr);

    if (!RTN_Valid(rtn)) {
        PIN_UnlockClient();
        return;
    }

    // Find the function name of the incoming address.
    string func_name =  RTN_Name(rtn);

    RTN_Open(rtn);

    // decides where to insert the injection calls and what calls to inject
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {

        if (!isValidInst(ins)) {
            continue;
        }

        #ifdef NOBRANCHES
        if (INS_IsBranch(ins) || !INS_HasFallThrough(ins)) {
            continue;
        }
        #endif

        // select instruction based on instruction type
        if (!isInstFITarget(ins)) {
            continue;
        }
        
        int numW = INS_MaxNumWRegs(ins), randW = 0;
        UINT32 index = 0;
        REG reg;

        if (numW > 1) {
            randW = random() % numW;
        }
        else {
            randW = 0;
            //continue; // No writable registers, skip injection.
        }

        // Jiesheng
        reg = INS_RegW(ins, randW);

        if (numW > 1 && (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS))
            randW = (randW + 1) % numW;
        if (numW > 1 && REG_valid(INS_RegW(ins, randW)))
            reg = INS_RegW(ins, randW);
        else
            reg = INS_RegW(ins, 0);
        
        if (!REG_valid(reg)) {

            LOG("REGNOTVALID: inst " + INS_Disassemble(ins) + "\n");
            continue;
        }
        index = reg_map.findRegIndex(reg);
        LOG("ins:" + INS_Disassemble(ins) + "\n");
        LOG("reg:" + REG_StringShort(reg) + "\n");

        
        // Jiesheng Wei
        if (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS) {
            INS next_ins = INS_Next(ins);
            if (INS_Valid(next_ins) && INS_Category(next_ins) == XED_CATEGORY_COND_BR) {
                //LOG("inject flag bit:" + REG_StringShort(reg) + "\n");

                UINT32 jmpindex = jmp_map.findJmpIndex(OPCODE_StringShort(INS_Opcode(next_ins)));
                INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR) FI_InjectFault_FlagReg,
                    IARG_INST_PTR,
                    IARG_UINT32, index,
                    IARG_UINT32, jmpindex,
                    IARG_CONTEXT,
                    IARG_END);
                
            } else if (INS_IsMemoryWrite(ins)) {
                LOG("COMP2MEM: inst " + INS_Disassemble(ins) + "\n");

                INS_InsertCall(
                    ins, IPOINT_BEFORE, (AFUNPTR) FI_InjectFault_Mem,
                    IARG_ADDRINT, INS_Address(ins),
                    IARG_MEMORYREAD_EA,
                    IARG_MEMORYREAD_SIZE,
                    IARG_END);

            } else {
                LOG("NORMAL FLAG REG: inst " + INS_Disassemble(ins) + "\n");
            }

        }

       INS_InsertCall(
            ins, IPOINT_AFTER, (AFUNPTR) inject_CCS,
            IARG_ADDRINT, INS_Address(ins),
            IARG_UINT32, index,
            IARG_CONTEXT,
            IARG_END);
    }

    RTN_Close(rtn);

    PIN_UnlockClient();
}


/*ORHAN COMMENT*/
VOID Routine(RTN rtn, VOID *v) {

    if (!RTN_Valid(rtn)) {
        return;
    }

    ADDRINT addr = RTN_Address(rtn);

    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)functionEntry, IARG_ADDRINT, addr, IARG_END);
    RTN_Close(rtn);
}

/* ORHAN COMMENT
 * Called by pin for each new instruction.
 * Translated to instruction routine (func etc.).
 * If it is a valid routine, the count inst method is called.
*/
VOID CountInstRoutine(INS ins, VOID *v) {

    // decides where to insert the injection calls and what calls to inject
    if (!isValidInst(ins)) {
        return;
    }

    #ifdef NOBRANCHES
        if(INS_IsBranch(ins) || !INS_HasFallThrough(ins)) {
            //LOG("faultinject: branch/ret inst: " + INS_Disassemble(ins) + "\n");
		    return;
        }
    #endif

    // select instruction based on instruction type
    if(!isInstFITarget(ins)) {
        return;
    }

    int numW = INS_MaxNumWRegs(ins), randW = 0;
	UINT32 index = 0;
	REG reg;

    if(numW > 1) {
	    randW = random() % numW;
    } else {
        randW = 0;
    }

    // Jiesheng
    reg = INS_RegW(ins, randW);

    if(numW > 1 && (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS)) {
        randW = (randW + 1) % numW;
    }

    if(numW > 1 && REG_valid(INS_RegW(ins, randW))) {
        reg = INS_RegW(ins, randW);
    } else {
        reg = INS_RegW(ins, 0);
    }
    
    if(!REG_valid(reg)) {
        LOG("REGNOTVALID: inst " + INS_Disassemble(ins) + "\n");
        return;
    }

    index = reg_map.findRegIndex(reg);
    LOG("ins:" + INS_Disassemble(ins) + "\n"); 
	LOG("reg:" + REG_StringShort(reg) + "\n");

    // Jiesheng Wei
	if (reg == REG_RFLAGS || reg == REG_FLAGS || reg == REG_EFLAGS) {
		INS next_ins = INS_Next(ins);
		if (INS_Valid(next_ins) && INS_Category(next_ins) == XED_CATEGORY_COND_BR) {
            //LOG("inject flag bit:" + REG_StringShort(reg) + "\n");
			
            UINT32 jmpindex = jmp_map.findJmpIndex(OPCODE_StringShort(INS_Opcode(next_ins)));
			INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)FI_InjectFault_FlagReg,
						IARG_INST_PTR,
						IARG_UINT32, index,
						IARG_UINT32, jmpindex,
						IARG_CONTEXT,
						IARG_END);
		    return;
		} else if (INS_IsMemoryWrite(ins)) {
            LOG("COMP2MEM: inst " + INS_Disassemble(ins) + "\n");
				
            INS_InsertPredicatedCall(
								ins, IPOINT_BEFORE, (AFUNPTR)FI_InjectFault_Mem,
								IARG_ADDRINT, INS_Address(ins),
								IARG_MEMORYREAD_EA,							
								IARG_MEMORYREAD_SIZE,
								IARG_END);
            return;
    
        } else {
            LOG("NORMAL FLAG REG: inst " + INS_Disassemble(ins) + "\n");
        }
        
	}

    INS_InsertPredicatedCall(
                ins, IPOINT_AFTER, (AFUNPTR)inject_CCS,
                IARG_ADDRINT, INS_Address(ins),
                IARG_UINT32, index,	
                IARG_CONTEXT,
                IARG_END);      

}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage() {
    PIN_ERROR("This Pintool does fault injection\n" +
        KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

int main(int argc, char * argv[]) {

    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    configInstSelector();

    PRINTF("\n*** Started reading the file containing '%s' instructions", GetInstCountFileName().c_str());
    get_instance_number(GetInstCountFileName().c_str());

    if (!fiecc.Value()) {
        INS_AddInstrumentFunction(CountInstRoutine, 0);
    } else {
        INS_AddInstrumentFunction(instruction_InstrumentationECC, 0);
    }

    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}