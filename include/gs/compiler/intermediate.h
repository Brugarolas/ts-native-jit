#pragma once
#include <gs/common/types.h>
#include <gs/utils/SourceLocation.h>
#include <gs/compiler/Value.h>
#include <gs/compiler/types.h>

#include <utils/Array.h>
#include <utils/String.h>

namespace gs {
    class DataType;

    namespace compiler {
        //
        // 'vreg' refers to a val which represents a virtual register
        // 'imm' refers to a val which represents a hard-coded value
        // 'val' can mean either of the above two things
        //
        // The operands for all binary operations must be of the same
        // data type, with the exception of the result operand.
        // 
        enum ir_instruction {
            // Do nothing. Should be ignored when processing and translating to target arch
            ir_noop,

            // Labels address for instructions which represent jumps
            //
            // Label 0 will be the ID of the label
            ir_label,

            // Allocates space on the stack
            //
            // Operand 0 will be vreg which will receive a pointer to allocated space
            // Operand 1 will be u32 imm for allocation size in bytes
            // Operand 2 will be u32 imm for allocation ID
            ir_stack_allocate,

            // Frees space that was allocated on the stack
            //
            // Operand 0 will be u32 imm for allocation ID
            ir_stack_free,

            // Get pointer to preallocated space in module memory
            //
            // Operand 0 will be vreg which will receive a pointer to preallocated space
            // Operand 1 will be u32 imm module id
            // Operand 2 will be u32 imm offset into module memory
            ir_module_data,

            // Reserves a virtual register which will be assigned later via resolve
            // This instruction counts as an assignment. The reserve/resolve instructions
            // exist exclusively to influence code optimization and register allocation.
            // 
            // Operand 0 will be vreg which will be assigned later
            ir_reserve,

            // Resolves a pending assignment which was promised by the reserve instruction
            // This instruction is equivalent to the move instruction, with the following
            // exceptions:
            // - The relationship with the reserve instruction
            // - This instruction does not count as an assignment
            //
            // Operand 0 will be vreg which was promised an assignment
            // Operand 1 will be val who's value will be copied
            ir_resolve,

            // Load value from address
            //
            // Operand 0 will be vreg which will receive the loaded value
            // Operand 1 will be val which holds the address to load from
            ir_load,

            // Store value at address
            //
            // Operand 0 will be val which holds value to store
            // Operand 1 will be val which holds address to store value in
            ir_store,

            // Copies value from one val to another
            //
            // Operand 0 will be vreg which will receive the value
            // Operand 1 will be val which represents the value to be copied

            // Unconditional jump to a label
            //
            // Label 0 will be the label ID to jump to
            ir_jump,

            // Converts the value of a vreg from one data type to another
            //
            // Operand 0 will be the vreg to convert
            // Operand 1 will be u32 imm ID of the data type to convert to
            ir_cvt,

            // Specifies a parameter to a function call
            // Multiple parameters will be specified in left to right order
            //
            // Operand 0 will be val representing the parameter to pass
            // Operand 1 will be u32 imm ID of the function being called
            ir_param,

            // Calls a function
            //
            // Operand 0 will either be u32 imm ID of the function being called
            // or a vreg which holds a pointer to the function closure being called
            // Operand 1 will be u32 imm ID of the type of the function signature
            ir_call,

            // Returns from the current function
            //
            // Operand 0 will be val which will be returned by the function, if the
            // function does not return void, otherwise it will be unset
            ir_ret,

            // Branches to one of two labels based on the value of a vreg
            //
            // Operand 0 will be vreg which holds a boolean value
            // Label 0 will be the label to jump to if op0 is true
            // Label 1 will be the label to jump to if op0 is false
            ir_branch,

            ir_iadd, // op0 = op1 + op2 (signed integer)
            ir_uadd, // op0 = op1 + op2 (unsigned integer)
            ir_fadd, // op0 = op1 + op2 (32 bit floating point)
            ir_dadd, // op0 = op1 + op2 (64 bit floating point)
            ir_isub, // op0 = op1 - op2 (signed integer)
            ir_usub, // op0 = op1 - op2 (unsigned integer)
            ir_fsub, // op0 = op1 - op2 (32 bit floating point)
            ir_dsub, // op0 = op1 - op2 (64 bit floating point)
            ir_imul, // op0 = op1 * op2 (signed integer)
            ir_umul, // op0 = op1 * op2 (unsigned integer)
            ir_fmul, // op0 = op1 * op2 (32 bit floating point)
            ir_dmul, // op0 = op1 * op2 (64 bit floating point)
            ir_idiv, // op0 = op1 / op2 (signed integer)
            ir_udiv, // op0 = op1 / op2 (unsigned integer)
            ir_fdiv, // op0 = op1 / op2 (32 bit floating point)
            ir_ddiv, // op0 = op1 / op2 (64 bit floating point)
            ir_imod, // op0 = op1 % op2 (signed integer)
            ir_umod, // op0 = op1 % op2 (unsigned integer)
            ir_fmod, // op0 = op1 % op2 (32 bit floating point)
            ir_dmod, // op0 = op1 % op2 (64 bit floating point)
            ir_ilt,  // op0 = op1 < op2 (signed integer)
            ir_ult,  // op0 = op1 < op2 (unsigned integer)
            ir_flt,  // op0 = op1 < op2 (32 bit floating point)
            ir_dlt,  // op0 = op1 < op2 (64 bit floating point)
            ir_ilte, // op0 = op1 <= op2 (signed integer)
            ir_ulte, // op0 = op1 <= op2 (unsigned integer)
            ir_flte, // op0 = op1 <= op2 (32 bit floating point)
            ir_dlte, // op0 = op1 <= op2 (64 bit floating point)
            ir_igt,  // op0 = op1 > op2 (signed integer)
            ir_ugt,  // op0 = op1 > op2 (unsigned integer)
            ir_fgt,  // op0 = op1 > op2 (32 bit floating point)
            ir_dgt,  // op0 = op1 > op2 (64 bit floating point)
            ir_igte, // op0 = op1 >= op2 (signed integer)
            ir_ugte, // op0 = op1 >= op2 (unsigned integer)
            ir_fgte, // op0 = op1 >= op2 (32 bit floating point)
            ir_dgte, // op0 = op1 >= op2 (64 bit floating point)
            ir_ieq,  // op0 = op1 == op2 (signed integer)
            ir_ueq,  // op0 = op1 == op2 (unsigned integer)
            ir_feq,  // op0 = op1 == op2 (32 bit floating point)
            ir_deq,  // op0 = op1 == op2 (64 bit floating point)
            ir_ineq, // op0 = op1 != op2 (signed integer)
            ir_uneq, // op0 = op1 != op2 (unsigned integer)
            ir_fneq, // op0 = op1 != op2 (32 bit floating point)
            ir_dneq, // op0 = op1 != op2 (64 bit floating point)
            ir_iinc, // op0++ (signed integer)
            ir_uinc, // op0++ (unsigned integer)
            ir_finc, // op0++ (32 bit floating point)
            ir_dinc, // op0++ (64 bit floating point)
            ir_idec, // op0-- (signed integer)
            ir_udec, // op0-- (unsigned integer)
            ir_fdec, // op0-- (32 bit floating point)
            ir_ddec, // op0-- (64 bit floating point)
            ir_ineg, // op0 = -op1 (signed integer)
            ir_fneg, // op0 = -op1 (32 bit floating point)
            ir_dneg, // op0 = -op1 (64 bit floating point)
            ir_not,  // op0 = !op1
            ir_inv,  // op0 = ~op1
            ir_shl,  // op0 = op1 << op2
            ir_shr,  // op0 = op1 >> op2
            ir_land, // op0 = op1 && op2
            ir_band, // op0 = op1 & op2
            ir_lor,  // op0 = op1 || op2
            ir_bor,  // op0 = op1 | op2
            ir_xor   // op0 = op1 ^ op2
        };



        struct instruction {
            ir_instruction op;
            Value operands[3];
            label_id labels[2];
            SourceLocation src;
            u8 oCnt;
            u8 lCnt;

            /**
             * @brief Returns pointer to Value that would be assigned by this instruction
             * 
             * @return Pointer to Value if op refers to an assigning instruction, otherwise null
             */
            Value* assigns() const;

            /**
             * @brief Returns true if this instruction involves the specified vreg ID
             * 
             * @param reg vreg ID to check
             * @param excludeAssignment Whether to ignore involvement if the vreg ID is
             *                          only involved by being assigned to by operation
             * @return Whether or not the specified vreg ID is involved 
             */
            bool involves(vreg_id reg, bool excludeAssignment = false) const;
        };

        class ICodeHolder;
        class InstructionRef {
            public:
                InstructionRef(ICodeHolder* owner, u64 index, ir_instruction i);

                InstructionRef& instr(ir_instruction i);
                InstructionRef& op(const Value& v);
                InstructionRef& label(label_id l);
                void remove();

                utils::String toString() const;
                
            private:
                ICodeHolder* m_owner;
                u64 index;
        };

        class CompiledFunction {
            public:
                CompiledFunction();

            protected:
        };

        class CompiledFunction;
        class ICodeHolder {
            public:
                ICodeHolder();
                ~ICodeHolder();

                InstructionRef add(ir_instruction i);
                label_id label();
                void remove(const InstructionRef& i);
                void remove(u64 index);

                // Takes care of updating label and vreg IDs
                void append(const ICodeHolder& code);

                // Takes care of updating label and vreg IDs
                void inlineCall(const CompiledFunction& f, const utils::Array<Value>& params);

                Value& val(const utils::String& name, DataType* tp);
                Value val(DataType* tp);
                
                template <typename T>
                Value val();

                template <typename T>
                Value imm(T value);
            
            protected:
                utils::Array<instruction> m_instructions;
                label_id m_nextLabelId;
                vreg_id m_nextRegId;
        };

        class CompiledFunction : public ICodeHolder {
            public:
                CompiledFunction();

                Value stackAlloc(u32 size);
                void stackFree(const Value& val);
        };
    };
};