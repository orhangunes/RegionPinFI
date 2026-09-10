#include <pin.H>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <limits>
#include "instselector.h"
#include "utils.h"

// Global variables and setting
KNOB<string> target_func_name(KNOB_MODE_WRITEONCE, "pintool", "func", "", "Specify the function name to count instructions");

static std::map<string, std::vector<std::pair<UINT64, UINT64>>> function_instruction_ranges;
static string target_function = "";
static bool function_found = false;
static UINT64 global_inst_counter = 0;
static UINT64 current_start = 0;
static UINT64 current_end = 0;

#define PRINTF(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); }

//#define INCLUDEALLINST
//#define NOBRANCHES
//#define NOSTACKFRAMEOP
//#define ONLYFP


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

    current_start = 0; // Reset for each function call

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

        // Increment the instruction counter
        global_inst_counter++;

        // If the incoming function name matches the target function name, calculate the instruction range.
        if (func_name == target_function) {


            function_found = true; // The target function exists in the application.

            // Save the starting instruction count of the function
            if (current_start == 0) {
                current_start = global_inst_counter;
            }

            // Save the ending instruction count of the function
            current_end = global_inst_counter;
        }
    }
    
    // Save the range found at the end of the loop. This allows adding a new range in the next func call.
    if (current_start != 0) {
        auto& ranges = function_instruction_ranges[func_name];
        ranges.emplace_back(current_start, current_end);
        current_start = 0;
        current_end = 0;
    }
    
    RTN_Close(rtn);

    PIN_UnlockClient();
}


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
 * Called by Pin for each valid instruction.
 * Increments the global_inst_counter.
 * While counting instructions, if it's an external function, determines its instruction range.
*/
VOID countAllAndFuncInst(string func_name) {
    
    global_inst_counter++;

    // If the incoming function name matches the target function name, calculate the instruction range.
    if (func_name == target_function) {

        function_found = true; // The target function exists in the application.

        // Save the starting instruction count of the function
        if (current_start == 0) {
            current_start = global_inst_counter;
        }

        // Save the ending instruction count of the function
        current_end = global_inst_counter;

        // Range variable
        auto& ranges = function_instruction_ranges[func_name];
            
        // If the vector is not empty and the last range end instruction is valid, update the end range.
        // If the vector is not empty and the end inst count of the last range equals the previous value, a new range is not added, and the end count of the existing range is updated.
        if (!ranges.empty() && ranges.back().second == global_inst_counter - 1) {
            // back => Returns the last element of the stack. This allows modifying or reading this element.
            ranges.back().second = current_end;
        }
        // Add a new range
        else {
            /* emplace_back => Adds an object directly to the end of the stack (container). 
               This library creates the object in place, meaning the object is not created as a local object and then copied or moved to the container before being added. 
               This improves performance by preventing unnecessary copy or move operations.
               So, without needing to create an object beforehand, it creates the object itself in place and puts it directly into the stack.*/
            ranges.emplace_back(current_start, current_end);
        }
        
        current_start = 0; // Reset for the next call
    }
}

/* ORHAN COMMENT
 * Called by Pin for each new instruction. 
 * Translated to an instruction routine (func etc.).
 * If it's a valid routine, the count inst method is called.
*/
VOID CountInstRoutine(INS ins, VOID *v) {

    if (!isValidInst(ins)) {
        return;
    }

    #ifdef NOBRANCHES
        if(INS_IsBranch(ins) || !INS_HasFallThrough(ins)) {
            LOG("instcount: branch/ret inst: " + INS_Disassemble(ins) + "\n");
		    return;
        }
    #endif
  
    // select instruction based on instruction type
    if(!isInstFITarget(ins)) {
        return;
    }

    // Routine information the instruction belongs to (func etc.)
    RTN rtn = INS_Rtn(ins);

    // Is the routine valid?
    if (RTN_Valid(rtn)) {
        
        // Get the name of the routine
        string func_name = RTN_Name(rtn);
        
        // The method that dynamically counts the instruction of the valid routine is called.
        INS_InsertPredicatedCall(
                ins,                                     // Current instruction
                IPOINT_BEFORE,                           // Execute before the instruction, IPOINT_BEFORE was used instead of IPOINT_AFTER. This prevents errors in non-fall-through instructions like JMP.
                (AFUNPTR)countAllAndFuncInst,            // Function name
                IARG_PTR, (void *)RTN_Name(rtn).c_str(), // Pass the symbolic function name of the instruction as an argument
                IARG_END);                               // End of arguments

    }
    // If the instruction is not bound to a valid routine
    else {
        std::cerr << "Instruction is not bound to a valid routine. Address: 0x" 
                  << std::hex << INS_Address(ins) << std::endl;
    }
}

/*ORHAN COMMENT - Dynamic instcount file name determination function*/
std::string GetInstCountFileName() {
    std::string func_name = target_func_name.Value();
    if (func_name.empty()) {
        func_name = "default";
    }
    return "pin.instcount_" + func_name + ".txt";
}

VOID Fini(INT32 code, VOID *v) {

    // Dynamically set the instcount file name
    std::string instcount_file = GetInstCountFileName();

    if (!function_found) {
        std::cerr << "\n*** Error: Function '" << target_function << "' not found in the application.\n" << std::endl;
        remove(instcount_file.c_str()); // The created pin.instcount.txt file is deleted.
        exit(255); // Exit with error
    }

    std::ofstream OutFile;
    OutFile.open(instcount_file.c_str());
    OutFile.setf(std::ios::showbase);

    OutFile << "Total Instructions: " << global_inst_counter << std::endl;

    for (const auto &entry : function_instruction_ranges) {
        OutFile << "Function: " << entry.first << " - Instruction Ranges;" << std::endl;
        for (const auto &range : entry.second) {
            OutFile << "    [" << range.first << ", " << range.second << "]" << std::endl;
        }
    }

    OutFile.close();

    PRINTF("\n*** '%s' instruction count file created.\n\n", instcount_file.c_str());
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
int Usage() {
    std::cerr << "This Pintool counts the number of dynamic instructions executed in a specified function." << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

int main(int argc, char *argv[]) {

    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    configInstSelector();

    target_function = target_func_name.Value();

    if (target_function.empty()) {

        // Dynamically set the instcount file name
        std::string instcount_file = GetInstCountFileName();

        std::cerr << "\n*** Error: No function name provided. Use -func to specify the function name in application.\n" << std::endl;
        remove(instcount_file.c_str()); // Delete the previously created pin.instcount.txt file if it exists.
        return Usage();
    }

    PRINTF("\n*** Start instruction count for function name: %s", target_function.c_str());

    INS_AddInstrumentFunction(CountInstRoutine, 0);

    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}