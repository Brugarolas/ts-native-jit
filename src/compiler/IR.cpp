#include <gs/compiler/IR.h>
#include <gs/compiler/FunctionDef.hpp>
#include <gs/common/Function.h>
#include <gs/interfaces/ICodeHolder.h>

#include <utils/Array.hpp>

namespace gs {
    namespace compiler {
        //
        // Instruction
        //

        Instruction::Instruction(ir_instruction i, const SourceLocation& _src) : src(_src) {
            op = i;
            labels[0] = labels[1] = 0;
            oCnt = lCnt = 0;
            fn_operands[0] = fn_operands[1] = fn_operands[2] = nullptr;
        }

        Value* Instruction::assigns() const {
            return nullptr;
        }

        bool Instruction::involves(vreg_id reg, bool excludeAssignment) const {
            return false;
        }

        utils::String Instruction::toString() const {
            
            static const char* istr[] = {
                "noop",
                "label",
                "stack_allocate",
                "stack_free",
                "module_data",
                "reserve",
                "resolve",
                "load",
                "store",
                "jump",
                "cvt",
                "param",
                "call",
                "ret",
                "branch",
                "iadd",
                "uadd",
                "fadd",
                "dadd",
                "isub",
                "usub",
                "fsub",
                "dsub",
                "imul",
                "umul",
                "fmul",
                "dmul",
                "idiv",
                "udiv",
                "fdiv",
                "ddiv",
                "imod",
                "umod",
                "fmod",
                "dmod",
                "ilt",
                "ult",
                "flt",
                "dlt",
                "ilte",
                "ulte",
                "flte",
                "dlte",
                "igt",
                "ugt",
                "fgt",
                "dgt",
                "igte",
                "ugte",
                "fgte",
                "dgte",
                "ieq",
                "ueq",
                "feq",
                "deq",
                "ineq",
                "uneq",
                "fneq",
                "dneq",
                "iinc",
                "uinc",
                "finc",
                "dinc",
                "idec",
                "udec",
                "fdec",
                "ddec",
                "ineg",
                "fneg",
                "dneg",
                "not",
                "inv",
                "shl",
                "shr",
                "land",
                "band",
                "lor",
                "bor",
                "xor",
                "assign"
            };

            utils::String s = istr[op];
            for (u8 o = 0;o < oCnt;o++) {
                if (fn_operands[o]) {
                    ffi::Function* fn = fn_operands[o]->getOutput();
                    if (fn) s += utils::String::Format(" <Function %s>", fn->getFullyQualifiedName().c_str());
                    else s += utils::String::Format(" <Function %s>", fn_operands[o]->getName().c_str());
                } else s += " " + operands[o].toString();
            }

            for (u8 l = 0;l < lCnt;l++) {
                s += utils::String::Format(" LABEL_%d", labels[l]);
            }

            return s;
        }

        //
        // InstructionRef
        //
        
        InstructionRef::InstructionRef(ICodeHolder* owner, u32 index) {
            m_owner = owner;
            m_index = index;
        }

        InstructionRef& InstructionRef::instr(ir_instruction i) {
            m_owner->m_instructions[m_index].op = i;
            return *this;
        }

        InstructionRef& InstructionRef::op(const Value& v) {
            Instruction& i = m_owner->m_instructions[m_index];
            i.operands[i.oCnt++].reset(v);
            return *this;
        }

        InstructionRef& InstructionRef::op(FunctionDef* fn) {
            Instruction& i = m_owner->m_instructions[m_index];
            ffi::Function* f = fn->getOutput();

            if (f) i.operands[i.oCnt++].reset(fn->imm(f->getId()));
            else i.fn_operands[i.oCnt++] = fn;

            return *this;
        }

        InstructionRef& InstructionRef::label(label_id l) {
            Instruction& i = m_owner->m_instructions[m_index];
            i.labels[i.lCnt++] = l;
            return *this;
        }

        Value* InstructionRef::assigns() const {
            Instruction& i = m_owner->m_instructions[m_index];
            return i.assigns();
        }

        bool InstructionRef::involves(vreg_id reg, bool excludeAssignment) const {
            Instruction& i = m_owner->m_instructions[m_index];
            return i.involves(reg, excludeAssignment);
        }

        void InstructionRef::remove() {
            m_owner->remove(m_index);
        }

        utils::String InstructionRef::toString() const {
            return m_owner->m_instructions[m_index].toString();
        }
    };
};