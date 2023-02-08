#include <tsn/compiler/Compiler.h>
#include <tsn/compiler/FunctionDef.hpp>
#include <tsn/compiler/Parser.h>
#include <tsn/common/Context.h>
#include <tsn/common/Module.h>
#include <tsn/common/Function.h>
#include <tsn/common/DataType.h>
#include <tsn/common/TypeRegistry.h>
#include <tsn/common/FunctionRegistry.h>
#include <tsn/compiler/Value.hpp>
#include <tsn/compiler/Output.h>
#include <tsn/interfaces/IDataTypeHolder.hpp>
#include <tsn/interfaces/ICodeHolder.h>
#include <tsn/utils/function_match.h>

#include <utils/Array.hpp>

#include <stdarg.h>

using namespace utils;
using namespace tsn::ffi;

namespace tsn {
    namespace compiler {
        utils::String argListStr(const utils::Array<Value>& args) {
            utils::String out;
            for (u32 i = 0;i < args.size();i++) {
                if (i > 0) out += ", ";
                out += args[i].getType()->getName();
            }
            return out;
        }

        utils::String argListStr(const utils::Array<const DataType*>& argTps) {
            utils::String out;
            for (u32 i = 0;i < argTps.size();i++) {
                if (i > 0) out += ", ";
                out += argTps[i]->getName();
            }
            return out;
        }



        //
        // Compiler
        //

        Compiler::Compiler(Context* ctx, ParseNode* programTree) : IContextual(ctx), m_scopeMgr(this) {
            m_program = programTree;
            m_output = nullptr;
            m_curFunc = nullptr;
            m_curClass = nullptr;
            enterNode(programTree);
            m_scopeMgr.enter();
        }

        Compiler::~Compiler() {
            exitNode();
        }

        void Compiler::enterNode(ParseNode* n) {
            n->computeSourceLocationRange();
            m_nodeStack.push(n);
        }

        void Compiler::exitNode() {
            m_nodeStack.pop();
        }

        void Compiler::enterFunction(FunctionDef* fn) {
            m_scopeMgr.enter();
            m_funcStack.push(fn);
            m_curFunc = fn;
            fn->onEnter();
        }

        Function* Compiler::exitFunction() {
            if (m_funcStack.size() == 0) return nullptr;
            FunctionDef* fd = m_funcStack.last();
            Function* fn = fd->onExit();
            if (fn) fn->m_src = fd->getSource();
            m_funcStack.pop();
            m_scopeMgr.exit();
            if (m_funcStack.size() == 0) m_curFunc = nullptr;
            else m_curFunc = m_funcStack[m_funcStack.size() - 1];
            return fn;
        }

        FunctionDef* Compiler::currentFunction() const {
            return m_curFunc;
        }

        DataType* Compiler::currentClass() const {
            return m_curClass;
        }

        ParseNode* Compiler::currentNode() {
            return m_nodeStack.last();
        }

        bool Compiler::inInitFunction() const {
            return m_funcStack.size() == 1;
        }

        const SourceLocation& Compiler::getCurrentSrc() const {
            return m_nodeStack[m_nodeStack.size() - 1]->tok.src;
        }

        ScopeManager& Compiler::scope() {
            return m_scopeMgr;
        }
        
        CompilerOutput* Compiler::getOutput() {
            return m_output;
        }

        CompilerOutput* Compiler::compile() {
            Module* m = new Module(m_ctx, "test");
            m_output = new CompilerOutput(this, m);

            // init function
            FunctionDef* fd = m_output->newFunc("__init__");
            enterFunction(fd);

            const utils::Array<tsn::ffi::DataType*>& importTypes = m_ctx->getGlobal()->allTypes();
            for (auto* t : importTypes) {
                m_output->import(t, t->getName());
            }

            const utils::Array<tsn::ffi::Function*>& importFuncs = m_ctx->getGlobal()->allFunctions();
            for (auto* f : importFuncs) {
                if (!f) continue;
                m_output->import(f, f->getName());
            }

            ParseNode* n = m_program->body;
            while (n) {
                if (n->tp == nt_import) {
                    compileImport(n);
                } else if (n->tp == nt_function) {
                    m_output->newFunc(n->str());
                }
                n = n->next;
            }

            n = m_program->body;
            while (n) {
                compileAny(n);
                n = n->next;
            }

            exitFunction();
            return m_output;
        }
        
        void Compiler::updateMethod(DataType* classTp, Method* m) {
            m->setThisType(classTp);
        }
        
        static compilation_message null_msg = {
            cmt_error,
            (compilation_message_code)0,
            utils::String(),
            SourceLocation(),
            nullptr
        };

        compilation_message& Compiler::typeError(ffi::DataType* tp, compilation_message_code code, const char* msg, ...) {
            if (tp->isEqualTo(currentFunction()->getPoison().getType())) {
                // If the type is poisoned, an error was already emitted. All (or most) subsequent
                // errors would not actually be errors if the original error had not occurred. This
                // should prevent a cascade of irrelevant errors.
                return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);

            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::typeError(ParseNode* node, ffi::DataType* tp, compilation_message_code code, const char* msg, ...) {
            if (tp->isEqualTo(currentFunction()->getPoison().getType())) {
                // If the type is poisoned, an error was already emitted. All (or most) subsequent
                // errors would not actually be errors if the original error had not occurred. This
                // should prevent a cascade of irrelevant errors.
                return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::valueError(const Value& val, compilation_message_code code, const char* msg, ...) {
            if (val.getType()->isEqualTo(currentFunction()->getPoison().getType())) {
                // If the type is poisoned, an error was already emitted. All (or most) subsequent
                // errors would not actually be errors if the original error had not occurred. This
                // should prevent a cascade of irrelevant errors.
                return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::valueError(ParseNode* node, const Value& val, compilation_message_code code, const char* msg, ...) {
            if (val.getType()->isEqualTo(currentFunction()->getPoison().getType())) {
                // If the type is poisoned, an error was already emitted. All (or most) subsequent
                // errors would not actually be errors if the original error had not occurred. This
                // should prevent a cascade of irrelevant errors.
                return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::functionError(ffi::DataType* selfTp, ffi::DataType* retTp, const utils::Array<Value>& args, compilation_message_code code, const char* msg, ...) {
            // If any of the involved types are poisoned, an error was already emitted. All
            // (or most) subsequent errors would not actually be errors if the original error
            // had not occurred. This should prevent a cascade of irrelevant errors.
            ffi::DataType* poisonTp = currentFunction()->getPoison().getType();

            if (retTp && retTp->isEqualTo(poisonTp)) return null_msg;
            if (selfTp && selfTp->isEqualTo(poisonTp)) return null_msg;
            for (u32 i = 0;i < args.size();i++) {
                if (args[i].getType()->isEqualTo(poisonTp)) return null_msg;
            }
            
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);

            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::functionError(ParseNode* node, ffi::DataType* selfTp, ffi::DataType* retTp, const utils::Array<Value>& args, compilation_message_code code, const char* msg, ...) {
            // If any of the involved types are poisoned, an error was already emitted. All
            // (or most) subsequent errors would not actually be errors if the original error
            // had not occurred. This should prevent a cascade of irrelevant errors.
            ffi::DataType* poisonTp = currentFunction()->getPoison().getType();

            if (retTp && retTp->isEqualTo(poisonTp)) return null_msg;
            if (selfTp && selfTp->isEqualTo(poisonTp)) return null_msg;
            for (u32 i = 0;i < args.size();i++) {
                if (args[i].getType()->isEqualTo(poisonTp)) return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::functionError(ffi::DataType* selfTp, ffi::DataType* retTp, const utils::Array<const ffi::DataType*>& argTps, compilation_message_code code, const char* msg, ...) {
            // If any of the involved types are poisoned, an error was already emitted. All
            // (or most) subsequent errors would not actually be errors if the original error
            // had not occurred. This should prevent a cascade of irrelevant errors.
            ffi::DataType* poisonTp = currentFunction()->getPoison().getType();

            if (retTp && retTp->isEqualTo(poisonTp)) return null_msg;
            if (selfTp && selfTp->isEqualTo(poisonTp)) return null_msg;
            for (u32 i = 0;i < argTps.size();i++) {
                if (argTps[i]->isEqualTo(poisonTp)) return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);

            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::functionError(ParseNode* node, ffi::DataType* selfTp, ffi::DataType* retTp, const utils::Array<const ffi::DataType*>& argTps, compilation_message_code code, const char* msg, ...) {
            // If any of the involved types are poisoned, an error was already emitted. All
            // (or most) subsequent errors would not actually be errors if the original error
            // had not occurred. This should prevent a cascade of irrelevant errors.
            ffi::DataType* poisonTp = currentFunction()->getPoison().getType();

            if (retTp && retTp->isEqualTo(poisonTp)) return null_msg;
            if (selfTp && selfTp->isEqualTo(poisonTp)) return null_msg;
            for (u32 i = 0;i < argTps.size();i++) {
                if (argTps[i]->isEqualTo(poisonTp)) return null_msg;
            }

            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::error(compilation_message_code code, const char* msg, ...) {
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);

            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }
        
        compilation_message& Compiler::error(ParseNode* node, compilation_message_code code, const char* msg, ...) {
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_error,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::warn(compilation_message_code code, const char* msg, ...) {
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);

            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_warn,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::warn(ParseNode* node, compilation_message_code code, const char* msg, ...) {
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_warn,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::info(compilation_message_code code, const char* msg, ...) {
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);

            ParseNode* n = m_nodeStack.last();
            m_messages.push({
                cmt_info,
                code,
                utils::String(out, len),
                n->tok.src,
                n->clone()
            });

            return m_messages.last();
        }

        compilation_message& Compiler::info(ParseNode* node, compilation_message_code code, const char* msg, ...) {
            char out[1024] = { 0 };
            va_list l;
            va_start(l, msg);
            i32 len = vsnprintf(out, 1024, msg, l);
            va_end(l);
            
            m_messages.push({
                cmt_info,
                code,
                utils::String(out, len),
                node->tok.src,
                node->clone()
            });

            return m_messages.last();
        }

        const utils::Array<compilation_message>& Compiler::getLogs() const {
            return m_messages;
        }



        InstructionRef Compiler::add(ir_instruction inst) {
            return m_curFunc->add(inst);
        }

        void Compiler::constructObject(const Value& dest, ffi::DataType* tp, const utils::Array<Value>& args) {
            Array<DataType*> argTps = args.map([](const Value& v) { return v.getType(); });

            if (tp->getInfo().is_primitive) {
                if (args.size() > 1) {
                    functionError(
                        tp,
                        nullptr,
                        args,
                        cm_err_no_matching_constructor,
                        "Type '%s' has no constructor with arguments matching '%s'",
                        tp->getName().c_str(),
                        argListStr(args).c_str()
                    );
                } else if (args.size() == 1) {
                    Value v = args[0].convertedTo(tp);
                    currentFunction()->add(ir_store).op(v).op(dest);
                }
                return;
            }

            Array<Function *> ctors = tp->findMethods("constructor", nullptr, (const DataType**)argTps.data(), argTps.size(), fm_skip_implicit_args);
            if (ctors.size() == 1) {
                if (ctors[0]->getAccessModifier() == private_access && (!m_curClass || !m_curClass->isEqualTo(tp))) {
                    error(cm_err_private_constructor, "Constructor '%s' is private", ctors[0]->getDisplayName().c_str());
                    return;
                } else generateCall(ctors[0], args, &dest);
            } else if (ctors.size() > 1) {
                ffi::Function* func = nullptr;
                u8 looseC = 0;
                for (u32 i = 0;i < ctors.size();i++) {
                    if (ctors[i]->getAccessModifier() == private_access && (!m_curClass || !m_curClass->isEqualTo(tp))) continue;
                    looseC++;
                    func = ctors[i];
                }

                if (looseC == 1) {
                    generateCall(func, args, &dest);
                    return;
                } else if (looseC == 0) {
                    functionError(
                        tp,
                        nullptr,
                        args,
                        cm_err_no_matching_constructor,
                        "Type '%s' has no accessible constructor with arguments matching '%s'",
                        tp->getName().c_str(),
                        argListStr(args).c_str()
                    );
                    return;
                }
                
                u8 strictC = 0;
                for (u32 i = 0;i < ctors.size();i++) {
                    if (ctors[i]->getAccessModifier() == private_access && (!m_curClass || !m_curClass->isEqualTo(tp))) continue;
                    const auto& args = ctors[i]->getSignature()->getArguments();
                    bool isCompatible = true;
                    for (u32 a = 0;a < args.size();a++) {
                        if (args[a].isImplicit()) continue;
                        if (!args[a].dataType->isEqualTo(argTps[a])) {
                            isCompatible = false;
                            break;
                        
                        }
                        if (args.size() > a + 1) {
                            // Function has more arguments which are not explicitly required
                            break;
                        }
                    }

                    if (isCompatible) {
                        strictC++;
                        func = ctors[i];
                    }
                }
                
                if (strictC == 1) {
                    generateCall(func, args, &dest);
                    return;
                }

                functionError(
                    tp,
                    nullptr,
                    args,
                    cm_err_ambiguous_constructor,
                    "Call to construct object of type '%s' with arguments '%s' is ambiguous",
                    tp->getName().c_str(),
                    argListStr(args).c_str()
                );

                for (u32 i = 0;i < ctors.size();i++) {
                    if (ctors[i]->getAccessModifier() == private_access && (!m_curClass || !m_curClass->isEqualTo(tp))) continue;
                    info(
                        cm_info_could_be,
                        "^ Could be: '%s'",
                        ctors[i]->getDisplayName().c_str()
                    ).src = ctors[i]->getSource();
                }
            } else {
                functionError(
                    tp,
                    nullptr,
                    args,
                    cm_err_no_matching_constructor,
                    "Type '%s' has no constructor with arguments matching '%s'",
                    tp->getName().c_str(),
                    argListStr(args).c_str()
                );
            }
        }

        void Compiler::constructObject(const Value& dest, const utils::Array<Value>& args) {
            constructObject(dest, dest.getType(), args);
        }

        Value Compiler::constructObject(ffi::DataType* tp, const utils::Array<Value>& args) {
            FunctionDef* cf = currentFunction();
            alloc_id stackId = cf->reserveStackId();

            Value out = currentFunction()->val(tp);
            cf->setStackId(out, stackId);
            cf->add(ir_stack_allocate).op(out).op(cf->imm(tp->getInfo().size)).op(cf->imm(stackId));

            constructObject(out, args);
            return out;
        }

        Value Compiler::functionValue(FunctionDef* fn) {
            return currentFunction()->imm(fn);
        }

        Value Compiler::functionValue(ffi::Function* fn) {
            return currentFunction()->imm(m_output->getFunctionDef(fn));
        }
        
        Value Compiler::typeValue(ffi::DataType* tp) {
            return currentFunction()->imm(tp);
        }
        
        Value Compiler::moduleValue(Module* mod) {
            return currentFunction()->imm(mod);
        }

        Value Compiler::moduleData(Module* m, u32 slot) {
            return currentFunction()->val(m, slot);
        }

        Value Compiler::generateCall(const Value& fn, const utils::String& name, ffi::DataType* retTp, const utils::Array<function_argument>& fargs, const utils::Array<Value>& params, const Value* self) {
            FunctionDef* cf = currentFunction();

            u8 argc = 0;
            for (u8 a = 0;a < fargs.size();a++) {
                if (!fargs[a].isImplicit()) argc++;
            }

            if (params.size() != argc) {
                if (name.size() > 0) {
                    functionError(
                        self ? self->getType() : nullptr,
                        retTp,
                        params,
                        cm_err_function_argument_count_mismatch,
                        "Function '%s' expects %d arguments, but %d %s provided",
                        name.c_str(),
                        argc,
                        params.size(),
                        params.size() == 1 ? "was" : "were"
                    );
                } else {
                    functionError(
                        self ? self->getType() : nullptr,
                        retTp,
                        params,
                        cm_err_function_argument_count_mismatch,
                        "Function expects %d arguments, but %d %s provided",
                        argc,
                        params.size(),
                        params.size() == 1 ? "was" : "were"
                    );
                }

                return cf->getPoison();
            }

            u8 aidx = 0;
            for (u8 a = 0;a < fargs.size();a++) {
                const auto& ainfo = fargs[a];
                if (ainfo.isImplicit()) continue;

                if (!params[aidx].getType()->isConvertibleTo(ainfo.dataType)) {
                    if (name.size() > 0) {
                        functionError(
                            self ? self->getType() : nullptr,
                            retTp,
                            params,
                            cm_err_type_not_convertible,
                            "Function '%s' is not callable with args '%s'",
                            name.c_str(),
                            argListStr(params).c_str()
                        );
                    } else {
                        functionError(
                            self ? self->getType() : nullptr,
                            retTp,
                            params,
                            cm_err_type_not_convertible,
                            "Function is not callable with args '%s'",
                            argListStr(params).c_str()
                        );
                    }
                    info(
                        cm_info_arg_conversion,
                        "^ No conversion found from type '%s' to type '%s' for argument %d",
                        params[aidx].getType()->getName().c_str(),
                        ainfo.dataType->getName().c_str(),
                        aidx + 1
                    );

                    return currentFunction()->getPoison();
                }
                aidx++;
            }
            
            Value result = cf->val(retTp);
            if (!retTp->getInfo().is_primitive && retTp->getInfo().size != 0) {
                alloc_id stackId = cf->reserveStackId();

                cf->setStackId(result, stackId);
                cf->add(ir_stack_allocate).op(result).op(cf->imm(retTp->getInfo().size)).op(cf->imm(stackId));
            }

            aidx = 0;
            for (u8 a = 0;a < fargs.size();a++) {
                const auto& arg = fargs[a];
                if (arg.isImplicit()) {
                    if (arg.argType == arg_type::context_ptr) cf->add(ir_param).op(cf->getECtx());
                    else if (arg.argType == arg_type::func_ptr) cf->add(ir_param).op(cf->imm<u64>(0));
                    else if (arg.argType == arg_type::ret_ptr) cf->add(ir_param).op(result);
                    else if (arg.argType == arg_type::this_ptr) {
                        if (!self) {
                            // TODO
                            // this may be bound already (think obj.method.bind(obj) in JS)
                            cf->add(ir_param).op(cf->imm<u64>(0));
                        } else cf->add(ir_param).op(*self);
                    }
                    continue;
                }
                cf->add(ir_param).op(params[aidx++]);
            }

            auto call = cf->add(ir_call).op(fn);
            if (retTp->getInfo().is_primitive && retTp->getInfo().size != 0) {
                call.op(result);
                return result;
            }

            return cf->getPoison();
        }
        
        Value Compiler::generateCall(Function* fn, const utils::Array<Value>& args, const Value* self) {
            FunctionType* sig = fn->getSignature();
            return generateCall(functionValue(fn), fn->getName(), sig->getReturnType(), sig->getArguments(), args, self);
        }

        Value Compiler::generateCall(FunctionDef* fn, const utils::Array<Value>& args, const Value* self) {
            return generateCall(functionValue(fn), fn->getName(), fn->getReturnType(), fn->getArgs(), args, self);
        }

        Value Compiler::generateCall(const Value& fn, const utils::Array<Value>& args, const Value* self) {
            if (fn.getFlags().is_function) {
                FunctionDef* fd = fn.getImm<FunctionDef*>();
                return generateCall(functionValue(fd), fd->getName(), fd->getReturnType(), fd->getArgs(), args, self);
            }

            FunctionType* sig = (FunctionType*)fn.getType();
            return generateCall(fn, fn.getName(), sig->getReturnType(), sig->getArguments(), args, self);
        }

        //
        // Data type resolution
        //

        DataType* getTypeFromSymbol(symbol* s) {
            if (!s) return nullptr;
            for (u32 i = 0;i < s->values.size();i++) {
                Value& v = *s->values[i];
                if (v.getFlags().is_type) return v.getType();
            }

            return nullptr;
        }
        
        DataType* Compiler::getArrayType(DataType* elemTp) {
            String name = "Array<" + elemTp->getName() + ">";
            symbol* s = scope().getBase().get(name);

            if (s) {
                DataType* tp = getTypeFromSymbol(s);
                if (tp) return tp;
                else {
                    error(
                        cm_err_internal,
                        "'Array<%s>' does not name a data type",
                        elemTp->getName().c_str()
                    );

                    return currentFunction()->getPoison().getType();
                }
            } else {
                String fullName = "Array<" + elemTp->getFullyQualifiedName() + ">";
                TemplateType* at = (TemplateType*)scope().getBase().get("Array");
                ParseNode* arrayAst = at->getAST();
                Scope& tscope = scope().enter();
                DataType* outTp = nullptr;

                // Add the array element type to the symbol table as the template argument name
                tscope.add(arrayAst->template_parameters->str(), elemTp);

                if (arrayAst->tp == nt_type) {
                    outTp = resolveTypeSpecifier(arrayAst->data_type);
                } else if (arrayAst->tp == nt_class) {
                    // todo
                    outTp = compileClass(arrayAst, true);
                }
                
                getOutput()->getModule()->addForeignType(outTp);

                scope().exit();
                return outTp;
            }

            return nullptr;
        }
        DataType* Compiler::getPointerType(DataType* destTp) {
            String name = "Pointer<" + destTp->getName() + ">";
            symbol* s = scope().getBase().get(name);
            
            if (s) {
                DataType* tp = getTypeFromSymbol(s);
                if (tp) return tp;
                
                error(
                    cm_err_internal,
                    "'Pointer<%s>' does not name a data type",
                    destTp->getName().c_str()
                );

                return currentFunction()->getPoison().getType();
            } else {
                String fullName = "Pointer<" + destTp->getFullyQualifiedName() + ">";
                TemplateType* pt = (TemplateType*)scope().getBase().get("Pointer");
                ParseNode* pointerAst = pt->getAST();
                Scope& tscope = scope().enter();

                // Add the pointer destination type to the symbol table as the template argument name
                tscope.add(pointerAst->template_parameters->str(), destTp);

                DataType* outTp = nullptr;
                if (pointerAst->tp == nt_type) {
                    outTp = resolveTypeSpecifier(pointerAst->data_type);
                } else if (pointerAst->tp == nt_class) {
                    // todo
                    outTp = compileClass(pointerAst, true);
                }

                getOutput()->getModule()->addForeignType(outTp);

                scope().exit();

                return outTp;
            }

            return nullptr;
        }
        DataType* Compiler::resolveTemplateTypeSubstitution(ParseNode* templateArgs, DataType* _type) {
            if (!_type->getInfo().is_template) {
                typeError(
                    _type,
                    cm_err_template_type_expected,
                    "Type '%s' is not a template type but template arguments were provided",
                    _type->getName().c_str()
                );

                return currentFunction()->getPoison().getType();
            }

            TemplateType* type = (TemplateType*)_type;

            ParseNode* tpAst = type->getAST();
            enterNode(tpAst);
            Scope& s = scope().enter();

            // Add template parameters to symbol table, representing the provided arguments
            // ...Also construct type name
            String name = _type->getName();
            String fullName = _type->getFullyQualifiedName();
            name += '<';
            fullName += '<';

            ParseNode* n = tpAst->template_parameters;
            u32 pc = 0;
            while (n) {
                enterNode(n);
                DataType* pt = resolveTypeSpecifier(templateArgs);
                s.add(n->str(), pt);
                name += pt->getName();
                fullName += pt->getFullyQualifiedName();
                if (n != tpAst->template_parameters) {
                    name += ", ";
                    fullName += ", ";
                }

                pc++;
                n = n->next;
                templateArgs = templateArgs->next;

                if (n && !templateArgs) {
                    u32 ec = pc;
                    ParseNode* en = n;
                    while (en) {
                        ec++;
                        en = en->next;
                    }

                    typeError(
                        type,
                        cm_err_too_few_template_args,
                        "Type '%s' expects %d template arguments but only %d were provided",
                        type->getName().c_str(),
                        ec,
                        pc
                    );

                    scope().exit();
                    exitNode();
                    return currentFunction()->getPoison().getType();
                }
                exitNode();
            }

            name += '>';
            fullName += '>';

            if (!n && templateArgs) {
                u32 ec = pc;
                ParseNode* pn = templateArgs;
                while (pn) {
                    pc++;
                    pn = pn->next;
                }

                typeError(
                    type,
                    cm_err_too_few_template_args,
                    "Type '%s' only expects %d template arguments but %d were provided",
                    type->getName().c_str(),
                    ec,
                    pc
                );
                scope().exit();
                exitNode();
                return currentFunction()->getPoison().getType();
            }

            symbol* existing = scope().getBase().get(name);
            if (existing) {
                DataType* etp = nullptr;
                for (u32 i = 0;i < existing->values.size();i++) {
                    Value& v = *existing->values[i];
                    if (v.getFlags().is_type) {
                        etp = v.getType();
                        break;
                    }
                }

                if (!etp) {
                    typeError(
                        type,
                        cm_err_symbol_already_exists,
                        "Symbol '%s' already exists and it is not a data type",
                        name.c_str()
                    );
                    scope().exit();
                    exitNode();
                    return currentFunction()->getPoison().getType();
                }

                scope().exit();
                exitNode();
                return etp;
            }

            DataType* tp = nullptr;
            if (tpAst->tp == nt_type) {
                tp = resolveTypeSpecifier(tpAst->data_type);
                tp = new AliasType(name, fullName, tp);
                tp->setAccessModifier(private_access);
                m_output->add(tp);
            } else if (tpAst->tp == nt_class) {
                tp = compileClass(tpAst, true);
            }
            
            scope().exit();
            exitNode();
            return tp;
        }
        DataType* Compiler::resolveObjectTypeSpecifier(ParseNode* n) {
            enterNode(n);
            Array<type_property> props;
            Function* dtor = nullptr;
            type_meta info = { 0 };
            info.is_pod = 1;
            info.is_trivially_constructible = 1;
            info.is_trivially_copyable = 1;
            info.is_trivially_destructible = 1;
            info.is_anonymous = 1;

            ParseNode* b = n->body;
            while (b) {
                switch (b->tp) {
                    case nt_type_property: {
                        DataType* pt = resolveTypeSpecifier(b->data_type);
                        props.push({
                            b->str(),
                            public_access,
                            info.size,
                            pt,
                            {
                                1, // can read
                                1, // can write
                                0, // static
                                0  // pointer
                            },
                            nullptr,
                            nullptr
                        });

                        const type_meta& i = pt->getInfo();
                        if (!i.is_pod) info.is_pod = 0;
                        if (!i.is_trivially_constructible) info.is_trivially_constructible = 0;
                        if (!i.is_trivially_copyable) info.is_trivially_copyable = 0;
                        if (!i.is_trivially_destructible) info.is_trivially_destructible = 0;
                        info.size += pt->getInfo().size;

                        break;
                    }
                    default: {
                        break;
                    }
                }

                b = b->next;
            }

            String name = String::Format("$anon_%d", getContext()->getTypes()->getNextAnonTypeIndex());
            DataType* tp = new DataType(
                name,
                name,
                info,
                props,
                {},
                dtor,
                {}
            );

            // anon types are always public
            tp->setAccessModifier(public_access);

            const Array<DataType*>& types = getContext()->getTypes()->allTypes();
            bool alreadyExisted = false;
            for (u32 i = 0;i < types.size();i++) {
                if (types[i]->getInfo().is_anonymous && types[i]->isEquivalentTo(tp)) {
                    delete tp;
                    tp = types[i];
                    alreadyExisted = true;
                }
            }

            if (!alreadyExisted) {
                m_output->add(tp);
            }

            exitNode();
            return tp;
        }
        DataType* Compiler::resolveFunctionTypeSpecifier(ParseNode* n) {
            enterNode(n);
            DataType* retTp = resolveTypeSpecifier(n->data_type);
            DataType* ptrTp = getContext()->getTypes()->getType<void*>();
            Array<function_argument> args;
            args.push({ arg_type::func_ptr, ptrTp });
            args.push({ arg_type::ret_ptr, retTp });
            args.push({ arg_type::context_ptr, ptrTp });

            ParseNode* a = n->parameters;
            while (a) {
                DataType* atp = resolveTypeSpecifier(a->data_type);
                args.push({
                    atp->getInfo().is_primitive ? arg_type::value : arg_type::pointer,
                    atp
                });
                a = a->next;
            }

            DataType* tp = new FunctionType(retTp, args);
            
            // function types are always public
            tp->setAccessModifier(public_access);

            m_output->add(tp);
            exitNode();
            return tp;
        }
        DataType* Compiler::resolveTypeNameSpecifier(ParseNode* n) {
            String name = n->body->str();
            symbol* s = scope().get(name);
            if (!s) {
                error(
                    n,
                    cm_err_identifier_not_found,
                    "Undefined identifier '%s'",
                    name.c_str()
                );

                return currentFunction()->getPoison().getType();
            }

            DataType* srcTp = nullptr;

            for (u32 i = 0;i < s->values.size();i++) {
                Value& v = *s->values[i];
                if (v.getFlags().is_type) {
                    srcTp = v.getType();
                    break;
                }  
            }

            if (!srcTp) {
                error(
                    n,
                    cm_err_identifier_does_not_refer_to_type,
                    "Identifier '%s' does not name a type",
                    name.c_str()
                );

                return currentFunction()->getPoison().getType();
            }

            if (n->body->template_parameters) {
                enterNode(n);
                DataType* tp = resolveTemplateTypeSubstitution(n->body->template_parameters, srcTp);
                exitNode();
                return tp;
            }

            return srcTp;
        }
        DataType* Compiler::applyTypeModifiers(DataType* tp, ParseNode* mod) {
            if (!mod) return tp;
            enterNode(mod);
            DataType* outTp = nullptr;

            if (mod->flags.is_array) outTp = getArrayType(tp);
            else if (mod->flags.is_pointer) outTp = getPointerType(tp);

            outTp = applyTypeModifiers(outTp, mod->modifier);
            exitNode();
            return outTp;
        }
        DataType* Compiler::resolveTypeSpecifier(ParseNode* n) {
            DataType* tp = nullptr;
            if (n->body && n->body->tp == nt_type_property) tp = resolveObjectTypeSpecifier(n);
            else if (n->parameters) tp = resolveFunctionTypeSpecifier(n);
            else tp = resolveTypeNameSpecifier(n);

            return applyTypeModifiers(tp, n->modifier);
        }

        DataType* Compiler::compileType(ParseNode* n) {
            enterNode(n);
            DataType* tp = nullptr;
            String name = n->str();

            if (n->template_parameters) {
                tp = new TemplateType(name, getOutput()->getModule()->getName() + "::" + name, n->clone());
                tp->setAccessModifier(private_access);
                m_output->add(tp);
            } else {
                tp = resolveTypeSpecifier(n->data_type);

                if (tp) {
                    tp = new AliasType(name, getOutput()->getModule()->getName() + "::" + name, tp);
                    tp->setAccessModifier(private_access);
                    m_output->add(tp);
                }
            }

            exitNode();
            return tp;
        }
        void Compiler::compileMethodDef(ParseNode* n, DataType* methodOf, Method* m) {
            updateMethod(methodOf, m);
            
            FunctionType* sig = m->getSignature();
            const auto& args = sig->getArguments();
            ParseNode* p = n->parameters;

            enterNode(n->body);

            FunctionDef* def = m_output->getFunctionDef(m);
            def->setThisType(methodOf);
            enterFunction(def);

            while (p) {
                def->addDeferredArg(p->str(), resolveTypeSpecifier(p->data_type));
                p = p->next;
            }

            compileBlock(n->body);

            exitFunction();
            exitNode();
        }
        Method* Compiler::compileMethodDecl(ParseNode* n, u64 thisOffset, bool* wasDtor, bool templatesDefined, bool dtorExists) {
            enterNode(n);
            utils::String name = n->str();
            DataType* voidTp = getContext()->getTypes()->getType<void>();
            DataType* ptrTp = getContext()->getTypes()->getType<void*>();

            bool isOperator = false;
            if (name == "operator") {
                isOperator = true;
                if (n->op != op_undefined) {
                    static const char* opStrs[] = {
                        "",
                        "+", "+=",
                        "-", "-=",
                        "*", "*=",
                        "/", "/=",
                        "%", "%=",
                        "^", "^=",
                        "&", "&=",
                        "|", "|=",
                        "~",
                        "<<", "<<=",
                        ">>", ">>=",
                        "!", "!=",
                        "&&", "&&=",
                        "||", "||=",
                        "=", "==",
                        "<", "<=",
                        ">", ">=",
                        "++", "++",
                        "--", "--",
                        "-",
                        "[]",
                        "?",
                        ".",
                        "new", "new",
                        "()"
                    };
                    name += opStrs[n->op];
                }
            }
            
            if (n->template_parameters) {
                if (!templatesDefined) {
                    Method* m = new TemplateMethod(
                        name,
                        n->flags.is_private ? private_access : public_access,
                        thisOffset,
                        n->clone()
                    );
                    m_output->newFunc(m);
                    exitNode();
                    return m;
                }

                name += '<';

                ParseNode* arg = n->template_parameters;
                while (arg) {
                    symbol* s = scope().get(arg->str());
                    // symbol for template parameter should always be defined here

                    DataType* pt = getTypeFromSymbol(s);
                    name += pt->getFullyQualifiedName();
                    if (arg != n->template_parameters) name += ", ";

                    arg = arg->next;
                }

                name += '>';
            }

            if (isOperator) {
                if (n->op == op_undefined) {
                    DataType* castTarget = resolveTypeSpecifier(n->data_type);
                    name += " " + castTarget->getFullyQualifiedName();
                }
            }

            bool isCtor = false;
            if (name == "destructor") {
                *wasDtor = true;
                if (dtorExists) {
                    error(
                        cm_err_dtor_already_exists,
                        "Destructor already exists for type '%s'",
                        currentClass()->getName().c_str()
                    );
                }
            } else if (name == "constructor") {
                isCtor = true;
            }

            DataType* ret = *wasDtor ? voidTp : (isCtor ? ptrTp : voidTp);
            if (n->data_type) {
                ret = resolveTypeSpecifier(n->data_type);
            }

            bool sigExisted = false;
            utils::Array<function_argument> args;
            args.push({ arg_type::func_ptr, ptrTp });
            args.push({ arg_type::ret_ptr, ret });
            args.push({ arg_type::context_ptr, ptrTp });

            // This type will be assigned later
            args.push({ arg_type::this_ptr, ptrTp });

            ParseNode* p = n->parameters;
            u8 a = u8(args.size());
            DataType* poisonTp = currentFunction()->getPoison().getType();
            Array<DataType*> argTps;
            Array<String> argNames;
            while (p) {
                DataType* argTp = poisonTp;
                if (!p->data_type) {
                    error(
                        cm_err_expected_method_argument_type,
                        "No type specified for argument '%s' of method '%s' of class '%s'",
                        p->str().c_str(),
                        name.c_str(),
                        currentClass()->getName().c_str()
                    );
                } else argTp = resolveTypeSpecifier(p->data_type);

                args.push({ argTp->getInfo().is_primitive ? arg_type::value : arg_type::pointer, argTp });
                argTps.push(argTp);
                argNames.push(p->str());

                p = p->next;
                a++;
            }

            FunctionType* sig = new FunctionType(ret, args);
            
            // function types are always public
            sig->setAccessModifier(public_access);

            const auto& types = m_output->getTypes();
            for (auto* t : types) {
                const auto& info = t->getInfo();
                if (!info.is_function) continue;
                if (sig->isEquivalentTo(t)) {
                    delete sig;
                    sig = (FunctionType*)t;
                    sigExisted = true;
                    break;
                }
            }

            if (!sigExisted) {
                const auto& types = getContext()->getTypes()->allTypes();
                for (auto* t : types) {
                    const auto& info = t->getInfo();
                    if (!info.is_function) continue;
                    if (sig->isEquivalentTo(t)) {
                        delete sig;
                        sig = (FunctionType*)t;
                        sigExisted = true;
                        break;
                    }
                }
            }

            if (!sigExisted) {
                m_output->add(sig);
            }

            Method* m = new Method(
                name,
                sig,
                n->flags.is_private ? private_access : public_access,
                nullptr,
                nullptr,
                thisOffset
            );

            m_output->newFunc(m, n->data_type != nullptr);
            exitNode();
            return m;
        }
        DataType* Compiler::compileClass(ParseNode* n, bool templatesDefined) {
            enterNode(n);
            utils::String name = n->str();
            String fullName = getOutput()->getModule()->getName() + "::" + name;
            
            if (n->template_parameters) {
                if (!templatesDefined) {
                    DataType* tp = new TemplateType(name, fullName, n->clone());
                    tp->setAccessModifier(private_access);
                    m_output->add(tp);
                    exitNode();
                    return tp;
                }

                name += '<';
                fullName += '<';

                ParseNode* arg = n->template_parameters;
                while (arg) {
                    symbol* s = scope().get(arg->str());
                    DataType* pt = getTypeFromSymbol(s);
                    // symbol for template parameter should always be defined here

                    name += pt->getName();
                    fullName += pt->getFullyQualifiedName();
                    if (arg != n->template_parameters) {
                        name += ", ";
                        fullName += ", ";
                    }

                    arg = arg->next;
                }

                name += '>';
                fullName += '>';
            }

            ClassType* tp = new ClassType(name, fullName);
            tp->setAccessModifier(private_access);
            m_curClass = tp;
            m_output->add(tp);
            scope().getBase().add(name, tp);
            scope().enter();
            
            ParseNode* inherits = n->inheritance;
            while (inherits) {
                // todo: Check added properties don't have name collisions with
                //       existing properties

                DataType* t = resolveTypeSpecifier(inherits);
                tp->addBase(t, public_access);
                inherits = inherits->next;
            }

            ParseNode* prop = n->body;
            u64 thisOffset = tp->getInfo().size;
            while (prop) {
                if (prop->tp != nt_property) {
                    prop = prop->next;
                    continue;
                }

                value_flags f = { 0 };
                f.can_read = 1;
                f.can_write = (!prop->flags.is_const) ? 1 : 0;
                f.is_pointer = prop->flags.is_pointer;
                f.is_static = prop->flags.is_static;

                DataType* t = resolveTypeSpecifier(prop->data_type);
                tp->addProperty(
                    prop->str(),
                    t,
                    f,
                    prop->flags.is_private ? private_access : public_access,
                    nullptr,
                    nullptr
                );

                prop = prop->next;
            }

            ParseNode* meth = n->body;
            ParseNode* dtor = nullptr;
            while (meth) {
                if (meth->tp != nt_function) {
                    meth = meth->next;
                    continue;
                }

                bool isDtor = false;
                Method* m = compileMethodDecl(meth, thisOffset, &isDtor, false, tp->getDestructor() != nullptr);
                m->m_src = meth->tok.src;

                if (isDtor) {
                    tp->setDestructor(m);
                    dtor = meth;
                }
                else tp->addMethod(m);

                meth = meth->next;
            }

            u32 midx = 0;
            const auto& methods = tp->getMethods();
            meth = n->body;
            while (meth) {
                if (meth->tp != nt_function) {
                    meth = meth->next;
                    continue;
                }

                Method* m = (Method*)(meth == dtor ? tp->getDestructor() : methods[midx++]);

                if (!m->isTemplate()) compileMethodDef(meth, tp, m);
                
                meth = meth->next;
            }
            m_curClass = nullptr;
            scope().exit();
            exitNode();

            return tp;
        }
        Function* Compiler::compileFunction(ParseNode* n, bool templatesDefined) {
            enterNode(n);
            utils::String name = n->str();
            
            if (n->template_parameters) {
                if (!templatesDefined) {
                    TemplateFunction* f = new TemplateFunction(
                        name,
                        n->flags.is_private ? private_access : public_access,
                        n->clone()
                    );
                    
                    f->m_src = n->tok.src;
                    m_output->newFunc(f);
                    exitNode();
                    return f;
                }

                name += '<';

                ParseNode* arg = n->template_parameters;
                while (arg) {
                    symbol* s = scope().get(arg->str());
                    // symbol for template parameter should always be defined here

                    DataType* pt = getTypeFromSymbol(s);
                    name += pt->getFullyQualifiedName();
                    if (arg != n->template_parameters) name += ", ";

                    arg = arg->next;
                }

                name += '>';
            }

            FunctionDef* fd = getOutput()->newFunc(name);

            enterFunction(fd);

            if (n->data_type) {
                fd->setReturnType(resolveTypeSpecifier(n->data_type));
            }

            ParseNode* p = n->parameters;
            DataType* poisonTp = currentFunction()->getPoison().getType();
            while (p) {
                DataType* tp = poisonTp;
                if (!p->data_type) {
                    error(
                        cm_err_expected_function_argument_type,
                        "No type specified for argument '%s' of function '%s'",
                        p->str().c_str(),
                        name.c_str()
                    );
                } else tp = resolveTypeSpecifier(p->data_type);

                fd->addArg(p->str(), tp);
                p = p->next;
            }

            compileBlock(n->body);
            
            Function* out = exitFunction();
            exitNode();

            return out;
        }
        void Compiler::compileExport(ParseNode* n) {
            enterNode(n);
            if (!inInitFunction()) {
                error(cm_err_export_not_in_root_scope, "'export' keyword encountered outside of root scope");
                exitNode();
                return;
            }

            if (n->body->tp == nt_type) {
                compileType(n->body)->setAccessModifier(public_access);
                exitNode();
                return;
            } else if (n->body->tp == nt_function) {
                compileFunction(n->body)->setAccessModifier(public_access);
                exitNode();
                return;
            } else if (n->body->tp == nt_variable) {
                u32 slot = 0;
                Value& var = compileVarDecl(n->body, &slot);
                m_output->getModule()->setDataAccess(slot, public_access);
                exitNode();
                return;
            } else if (n->body->tp == nt_class) {
                compileClass(n->body)->setAccessModifier(public_access);
                exitNode();
                return;
            }

            error(cm_err_export_invalid, "Expected type, function, class, or variable");
            exitNode();
        }
        void Compiler::compileImport(ParseNode* n) {
            enterNode(n);
            if (!inInitFunction()) {
                error(cm_err_import_not_in_root_scope, "'import' keyword encountered outside of root scope");
                exitNode();
                return;
            }

            Module* m = m_ctx->getModule(n->str());
            if (!m) {
                error(cm_err_import_module_not_found, "Module '%s' not found", n->str().c_str());
                exitNode();
                return;
            }

            if (n->body->tp == nt_import_module) {
                scope().getBase().add(n->body->str(), m);
            } else if (n->body->tp == nt_import_symbol) {
                ParseNode* sym = n->body;
                while (sym) {
                    DataType* tp = sym->data_type ? resolveTypeSpecifier(sym->data_type) : nullptr;
                    String name = sym->str();
                    utils::Array<Function*> funcs;
                    utils::Array<const module_data*> vars;
                    utils::Array<u32> var_slots;

                    m->getData().each([&name, &vars, &var_slots, tp](const module_data& m, u32 idx) {
                        if (m.access == private_access) return;
                        if (m.name != name) return;
                        if (tp && !m.type->isEquivalentTo(tp)) return;
                        vars.push(&m);
                        var_slots.push(idx);
                    });

                    if (tp) {
                        if (tp->getInfo().is_function) {
                            FunctionType* ftp = (FunctionType*)tp;
                            utils::Array<DataType*> args = ftp->getArguments().map([](const function_argument& a) { return a.dataType; });
                            funcs = m->findFunctions(
                                name,
                                ftp->getReturnType(),
                                const_cast<const DataType**>(args.data()),
                                args.size(),
                                fm_exclude_private | fm_strict
                            );
                        }
                        
                        if (funcs.size() + vars.size() > 1) {
                            error(
                                cm_err_export_ambiguous,
                                "Module '%s' has multiple exports named '%s' that match the type '%s'",
                                n->str().c_str(),
                                name.c_str(),
                                tp->getName().c_str()
                            );
                            for (u32 i = 0;i < funcs.size();i++) {
                                info(cm_info_could_be, "^ Could be '%s'", funcs[i]->getFullyQualifiedName().c_str());
                            }
                            for (u32 i = 0;i < vars.size();i++) {
                                info(cm_info_could_be, "^ Could be '%s %s'", vars[i]->type->getName().c_str(), vars[i]->name.c_str());
                            }
                        } else if (funcs.size() == 1) {
                            m_output->import(funcs[0], sym->alias ? sym->alias->str() : name);
                        } else if (vars.size() == 1) {
                            scope().getBase().add(sym->alias ? sym->alias->str() : name, m, var_slots[0]);
                        } else {
                            error(
                                cm_err_export_not_found,
                                "Module '%s' has no export named '%s' that matches the type '%s'",
                                n->str().c_str(),
                                name.c_str(),
                                tp->getName().c_str()
                            );
                        }
                    } else {
                        m->allFunctions().each([&funcs, &name](Function* fn) {
                            if (!fn) return;
                            if (fn->getAccessModifier() == private_access) return;
                            if (fn->getName() != name) return;
                            funcs.push(fn);
                        });

                        if (funcs.size() + vars.size() > 1) {
                            error(
                                cm_err_export_ambiguous,
                                "Module '%s' has multiple exports named '%s'",
                                n->str().c_str(),
                                name.c_str(),
                                tp->getName().c_str()
                            );
                            for (u32 i = 0;i < funcs.size();i++) {
                                info(cm_info_could_be, "^ Could be '%s'", funcs[i]->getFullyQualifiedName().c_str());
                            }
                            for (u32 i = 0;i < vars.size();i++) {
                                info(cm_info_could_be, "^ Could be '%s %s'", vars[i]->type->getName().c_str(), vars[i]->name.c_str());
                            }
                        } else if (funcs.size() == 1) {
                            m_output->import(funcs[0], sym->alias ? sym->alias->str() : name);
                        } else if (vars.size() == 1) {
                            scope().getBase().add(sym->alias ? sym->alias->str() : name, m, var_slots[0]);
                        } else {
                            error(
                                cm_err_export_not_found,
                                "Module '%s' has no export named '%s'",
                                n->str().c_str(),
                                name.c_str()
                            );
                        }
                    }

                    sym = sym->next;
                }
            }

            exitNode();
        }
        Value Compiler::compileConditionalExpr(ParseNode* n) {
            FunctionDef* cf = currentFunction();

            scope().enter();
            Value cond = compileExpression(n->cond);
            Value condBool = cond.convertedTo(m_ctx->getTypes()->getType<bool>());
            scope().exit(condBool);

            auto reserve = add(ir_reserve);
            auto branch = add(ir_branch).op(condBool);

            // Truth body
            scope().enter();
            branch.label(cf->label());
            Value lvalue = compileExpressionInner(n->lvalue);

            // jumps to just after the false body
            auto jump = add(ir_jump);

            // Output must be created now since only now do we have the
            // output type. No instructions are emitted as a result of
            // this so this is not contingent upon the condition being
            // evaluated to true
            DataType* tp = lvalue.getType();
            Value out = cf->val(tp);

            if (!tp->getInfo().is_primitive) {
                // If the result is not a primitive type then the output
                // of the conditional must be allocated on the stack and
                // constructed from the resulting value
                // 
                // That being the case, the resolve instruction should
                // be replaced with a stack_allocate instruction and no
                // resolve instruction should be added
                // 
                // The stack id must be set now (prior to any uses of
                // result), but it must be added to the parent block's
                // stack
                alloc_id stackId = cf->reserveStackId();
                cf->setStackId(out, stackId);
                reserve.instr(ir_stack_allocate).op(out).op(cf->imm(tp->getInfo().size)).op(cf->imm(stackId));

                constructObject(out, { lvalue });
                scope().exit(out);
            } else {
                reserve.op(out);
                add(ir_resolve).op(out).op(lvalue);
                scope().exit();
            }
            // End truth body

            // False body
            branch.label(cf->label());
            scope().enter();
            
            Value rvalue = compileExpressionInner(n->rvalue);

            if (!tp->getInfo().is_primitive) constructObject(out, { rvalue });
            else add(ir_resolve).op(out).op(rvalue);

            scope().exit();
            // End false body

            // join address
            jump.label(cf->label());

            return out;
        }
        Value Compiler::compileMemberExpr(ParseNode* n, bool topLevel, member_expr_hints* hints) {
            FunctionDef* cf = currentFunction();

            enterNode(n->lvalue);
            Value lv;
            if (n->lvalue->tp == nt_expression) {
                if (n->lvalue->op == op_member) lv.reset(compileMemberExpr(n->lvalue, false, hints));
                else lv.reset(compileExpression(n->lvalue));
            } else lv.reset(compileExpressionInner(n->lvalue));
            exitNode();
            
            enterNode(n->rvalue);
            Value out = lv.getProp(
                n->rvalue->str(),
                false,                                                                          // exclude inherited
                !m_curClass || lv.getFlags().is_module || !lv.getType()->isEqualTo(m_curClass), // exclude private
                false,                                                                          // exclude methods
                true,                                                                           // emit errors
                hints                                                                           // hints
            );
            exitNode();

            if (hints) hints->self = new Value(lv);

            return out;
        }
        Value Compiler::compileArrowFunction(ParseNode* n) {
            // todo
            return currentFunction()->getPoison();
        }
        Value Compiler::compileExpressionInner(ParseNode* n) {
            if (n->tp == nt_identifier) {
                symbol* s = scope().get(n->str());
                if (s) return *s->values.last();

                error(n, cm_err_identifier_not_found, "Undefined identifier '%s'", n->str().c_str());
                return currentFunction()->getPoison();
            } else if (n->tp == nt_literal) {
                switch (n->value_tp) {
                    case lt_u8: return currentFunction()->imm<u8>((u8)n->value.u);
                    case lt_u16: return currentFunction()->imm<u16>((u16)n->value.u);
                    case lt_u32: return currentFunction()->imm<u32>((u32)n->value.u);
                    case lt_u64: return currentFunction()->imm<u64>((u64)n->value.u);
                    case lt_i8: return currentFunction()->imm<i8>((i8)n->value.i);
                    case lt_i16: return currentFunction()->imm<i16>((i16)n->value.i);
                    case lt_i32: return currentFunction()->imm<i32>((i32)n->value.i);
                    case lt_i64: return currentFunction()->imm<i64>(n->value.i);
                    case lt_f32: return currentFunction()->imm<f32>((f32)n->value.f);
                    case lt_f64: return currentFunction()->imm<f64>(n->value.f);
                    case lt_string: {
                        return currentFunction()->getPoison(); // todo
                    }
                    case lt_template_string: {
                        return currentFunction()->getPoison(); // todo
                    }
                    case lt_object: {
                        return currentFunction()->getPoison(); // todo
                    }
                    case lt_array: {
                        return currentFunction()->getPoison(); // todo
                    }
                    case lt_null: return currentFunction()->getNull();
                    case lt_true: return currentFunction()->imm(true);
                    case lt_false: return currentFunction()->imm(false);
                }

                return currentFunction()->getPoison();
            } else if (n->tp == nt_sizeof) {
                DataType* tp = resolveTypeSpecifier(n->data_type);
                if (tp) return currentFunction()->imm(tp->getInfo().size);

                return currentFunction()->getPoison();
            } else if (n->tp == nt_this) {
                if (!m_curClass) {
                    error(n, cm_err_this_outside_class, "Use of 'this' keyword outside of class scope");
                    return currentFunction()->getPoison();
                }

                return currentFunction()->getThis();
            } else if (n->tp == nt_cast) {
                return compileExpressionInner(n->body).convertedTo(resolveTypeSpecifier(n->data_type));
            } else if (n->tp == nt_function) return compileArrowFunction(n);

            // todo: expression sequence

            switch (n->op) {
                case op_undefined: break;
                case op_add: return compileExpressionInner(n->lvalue) + compileExpressionInner(n->rvalue);
                case op_addEq: return compileExpressionInner(n->lvalue) += compileExpressionInner(n->rvalue);
                case op_sub: return compileExpressionInner(n->lvalue) - compileExpressionInner(n->rvalue);
                case op_subEq: return compileExpressionInner(n->lvalue) -= compileExpressionInner(n->rvalue);
                case op_mul: return compileExpressionInner(n->lvalue) * compileExpressionInner(n->rvalue);
                case op_mulEq: return compileExpressionInner(n->lvalue) *= compileExpressionInner(n->rvalue);
                case op_div: return compileExpressionInner(n->lvalue) / compileExpressionInner(n->rvalue);
                case op_divEq: return compileExpressionInner(n->lvalue) /= compileExpressionInner(n->rvalue);
                case op_mod: return compileExpressionInner(n->lvalue) % compileExpressionInner(n->rvalue);
                case op_modEq: return compileExpressionInner(n->lvalue) %= compileExpressionInner(n->rvalue);
                case op_xor: return compileExpressionInner(n->lvalue) ^ compileExpressionInner(n->rvalue);
                case op_xorEq: return compileExpressionInner(n->lvalue) ^= compileExpressionInner(n->rvalue);
                case op_bitAnd: return compileExpressionInner(n->lvalue) & compileExpressionInner(n->rvalue);
                case op_bitAndEq: return compileExpressionInner(n->lvalue) &= compileExpressionInner(n->rvalue);
                case op_bitOr: return compileExpressionInner(n->lvalue) | compileExpressionInner(n->rvalue);
                case op_bitOrEq: return compileExpressionInner(n->lvalue) |= compileExpressionInner(n->rvalue);
                case op_bitInv: return ~compileExpressionInner(n->lvalue);
                case op_shLeft: return compileExpressionInner(n->lvalue) << compileExpressionInner(n->rvalue);
                case op_shLeftEq: return compileExpressionInner(n->lvalue) <<= compileExpressionInner(n->rvalue);
                case op_shRight: return compileExpressionInner(n->lvalue) >> compileExpressionInner(n->rvalue);
                case op_shRightEq: return compileExpressionInner(n->lvalue) >>= compileExpressionInner(n->rvalue);
                case op_not: return !compileExpressionInner(n->lvalue);
                case op_notEq: return compileExpressionInner(n->lvalue) != compileExpressionInner(n->rvalue);
                case op_logAnd: return compileExpressionInner(n->lvalue) && compileExpressionInner(n->rvalue);
                case op_logAndEq: return compileExpressionInner(n->lvalue).operator_logicalAndAssign(compileExpressionInner(n->rvalue));
                case op_logOr: return compileExpressionInner(n->lvalue) || compileExpressionInner(n->rvalue);
                case op_logOrEq: return compileExpressionInner(n->lvalue).operator_logicalOrAssign(compileExpressionInner(n->rvalue));
                case op_assign: return compileExpressionInner(n->lvalue) = compileExpressionInner(n->rvalue);
                case op_compare: return compileExpressionInner(n->lvalue) == compileExpressionInner(n->rvalue);
                case op_lessThan: return compileExpressionInner(n->lvalue) < compileExpressionInner(n->rvalue);
                case op_lessThanEq: return compileExpressionInner(n->lvalue) <= compileExpressionInner(n->rvalue);
                case op_greaterThan: return compileExpressionInner(n->lvalue) > compileExpressionInner(n->rvalue);
                case op_greaterThanEq: return compileExpressionInner(n->lvalue) >= compileExpressionInner(n->rvalue);
                case op_preInc: return ++compileExpressionInner(n->lvalue);
                case op_postInc: return compileExpressionInner(n->lvalue)++;
                case op_preDec: return --compileExpressionInner(n->lvalue);
                case op_postDec: return compileExpressionInner(n->lvalue)--;
                case op_negate: return -compileExpressionInner(n->lvalue);
                case op_index: return compileExpressionInner(n->lvalue)[compileExpressionInner(n->rvalue)];
                case op_conditional: return compileConditionalExpr(n);
                case op_member: return compileMemberExpr(n);
                case op_new: {
                    DataType* type = resolveTypeSpecifier(n->data_type);

                    Array<Value> args;
                    ParseNode* p = n->parameters;
                    while (p) {
                        args.push(compileExpression(p));
                        p = p->next;
                    }

                    return constructObject(type, args);
                }
                case op_placementNew: {
                    DataType* type = resolveTypeSpecifier(n->data_type);

                    Array<Value> args;
                    ParseNode* p = n->parameters;
                    while (p) {
                        args.push(compileExpression(p));
                        p = p->next;
                    }

                    Value dest = compileExpressionInner(n->lvalue);
                    constructObject(dest, type, args);
                    return dest;
                }
                case op_call: {
                    member_expr_hints h;
                    h.for_func_call = true;
                    h.self = nullptr;
                    ParseNode* p = n->parameters;

                    while (p) {
                        h.args.push(compileExpression(p));
                        p = p->next;
                    }

                    Value callee;
                    
                    if (n->lvalue->tp == nt_expression && n->lvalue->op == op_member) {
                        callee.reset(compileMemberExpr(n->lvalue, true, &h));
                    } else callee.reset(compileExpressionInner(n->lvalue));

                    Value ret = callee(h.args, h.self);
                    if (h.self) delete h.self;
                    return ret;
                }
            }

            return currentFunction()->getPoison();
        }
        Value Compiler::compileExpression(ParseNode* n) {
            enterNode(n);
            scope().enter();

            Value out = compileExpressionInner(n);
            
            scope().exit(out);
            exitNode();

            return out;
        }
        void Compiler::compileIfStatement(ParseNode* n) {
            FunctionDef* cf = currentFunction();
            enterNode(n);

            scope().enter();
            Value cond = compileExpression(n->cond);
            Value condBool = cond.convertedTo(m_ctx->getTypes()->getType<bool>());
            scope().exit(condBool);

            auto branch = add(ir_branch).op(condBool).label(cf->label());
            
            scope().enter();
            compileAny(n->body);
            scope().exit();

            if (n->else_body) {
                auto jump = add(ir_jump);

                branch.label(cf->label());

                
                scope().enter();
                compileAny(n->else_body);
                scope().exit();

                jump.label(cf->label());
            } else branch.label(cf->label());

            exitNode();
        }
        void Compiler::compileReturnStatement(ParseNode* n) {
            enterNode(n);
            DataType* retTp = currentFunction()->getReturnType();

            if (n->body) {
                Value ret = compileExpression(n->body);

                if (retTp && currentFunction()->isReturnTypeExplicit()) {
                    add(ir_ret).op(ret.convertedTo(retTp));
                } else {
                    currentFunction()->setReturnType(ret.getType());
                    add(ir_ret).op(ret);
                }

                exitNode();
                return;
            }

            if (retTp && currentFunction()->isReturnTypeExplicit()) {
                error(
                    cm_err_function_must_return_a_value,
                    "Function '%s' must return a value of type '%s'",
                    currentFunction()->getName().c_str(),
                    retTp->getName().c_str()
                );
                add(ir_ret).op(currentFunction()->getPoison());
                exitNode();
                return;
            }

            currentFunction()->setReturnType(m_ctx->getTypes()->getType<void>());
            add(ir_ret);
            exitNode();
        }
        void Compiler::compileSwitchStatement(ParseNode* n) {
            // todo
        }
        void Compiler::compileThrow(ParseNode* n) {
            // todo
        }
        void Compiler::compileTryBlock(ParseNode* n) {
            // todo
        }
        Value& Compiler::compileVarDecl(ParseNode* n, u32* moduleSlot) {
            enterNode(n);
            Value* v = nullptr;
            Value init = n->initializer ? compileExpression(n->initializer) : currentFunction()->getPoison();
            DataType* dt = nullptr;

            if (n->body->data_type) dt = resolveTypeSpecifier(n->body->data_type);
            else {
                if (!n->initializer) {
                    error(
                        cm_err_could_not_deduce_type,
                        "Uninitilized variable declarations must have an explicit type specifier"
                    );
                    return currentFunction()->getPoison();
                }

                dt = init.getType();
            }

            if (inInitFunction()) {
                u32 slot = m_output->getModule()->addData(n->body->str(), dt, private_access);
                if (moduleSlot) *moduleSlot = slot;
                v = &currentFunction()->val(n->body->str(), slot);
            } else {
                v = &currentFunction()->val(n->body->str(), dt);
            }

            if (n->initializer) {
                if (dt->getInfo().is_primitive) *v = init.convertedTo(dt);
                else constructObject(*v, { init });
            } else if (!dt->getInfo().is_primitive) {
                constructObject(*v, {});
            }

            exitNode();
            return *v;
        }
        void Compiler::compileObjectDecompositor(ParseNode* n) {
            enterNode(n);
            FunctionDef* cf = currentFunction();

            Value source = compileExpression(n->initializer);
            
            ParseNode* p = n->body;
            while (p) {
                enterNode(p);
                Value init = source.getProp(p->str());

                DataType* dt = p->data_type ? resolveTypeSpecifier(p->data_type) : init.getType();
                Value* v = nullptr;

                if (inInitFunction()) {
                    u32 slot = m_output->getModule()->addData(n->body->str(), dt, private_access);
                    v = &currentFunction()->val(p->str(), slot);
                } else {
                    v = &currentFunction()->val(p->str(), dt);
                }

                if (dt->getInfo().is_primitive) *v = init.convertedTo(dt);
                else constructObject(*v, { init });

                exitNode();
                p = p->next;
            }

            exitNode();
        }
        void Compiler::compileLoop(ParseNode* n) {
            FunctionDef* cf = currentFunction();
            enterNode(n);
            Scope& s = scope().enter();

            // initializer will either be expression or variable decl
            if (n->initializer) compileAny(n->initializer);

            label_id loop_begin = cf->label();
            m_lsStack.push({
                true,       // is_loop
                loop_begin, // loop_begin
                {},         // pending_end_label
                &s          // outer_scope
            });

            if (n->flags.defer_cond == 0) {
                scope().enter();
                Value cond = compileExpression(n->cond);
                Value condBool = cond.convertedTo(m_ctx->getTypes()->getType<bool>());
                scope().exit(condBool);

                m_lsStack.last().pending_end_label.push(add(ir_branch).op(condBool).label(cf->label()));

                scope().enter();
                compileAny(n->body);
                scope().exit();

                if (n->modifier) {
                    scope().enter();
                    compileExpression(n->modifier);
                    scope().exit();
                }

                add(ir_jump).label(loop_begin);
            } else {
                if (n->modifier) {
                    scope().enter();
                    compileExpression(n->modifier);
                    scope().exit();
                }

                scope().enter();
                compileAny(n->body);
                scope().exit();

                scope().enter();
                Value cond = compileExpression(n->cond);
                Value condBool = cond.convertedTo(m_ctx->getTypes()->getType<bool>());
                scope().exit(condBool);

                m_lsStack.last().pending_end_label.push(add(ir_branch).op(condBool).label(loop_begin));
            }

            label_id loop_end = cf->label();
            m_lsStack.last().pending_end_label.each([loop_end](InstructionRef& i) {
                i.label(loop_end);
            });
            m_lsStack.pop();

            scope().exit();
            exitNode();
        }
        void Compiler::compileContinue(ParseNode* n) {
            if (m_lsStack.size() == 0) {
                error(n, cm_err_continue_scope, "'continue' used outside of loop scope");
                return;
            }

            auto& ls = m_lsStack.last();
            if (!ls.is_loop) {
                error(n, cm_err_continue_scope, "'continue' used outside of loop scope");
                return;
            }

            scope().emitScopeExitInstructions(*ls.outer_scope);
            add(ir_jump).label(ls.loop_begin);
        }
        void Compiler::compileBreak(ParseNode* n) {
            if (m_lsStack.size() == 0) {
                error(n, cm_err_continue_scope, "'break' used outside of loop or switch scope");
                return;
            }
            auto& ls = m_lsStack.last();

            scope().emitScopeExitInstructions(*ls.outer_scope);
            ls.pending_end_label.push(add(ir_jump));
        }
        void Compiler::compileBlock(ParseNode* n) {
            scope().enter();

            n = n->body;
            while (n) {
                compileAny(n);
                n = n->next;
            }

            scope().exit();
        }
        void Compiler::compileAny(ParseNode* n) {
            switch (n->tp) {
                case nt_break: return compileBreak(n);
                case nt_class: {
                    if (!inInitFunction()) {
                        error(n, cm_err_class_scope, "'class' used outside of root scope");
                        return;
                    }

                    compileClass(n);
                    return;
                }
                case nt_continue: return compileContinue(n);
                case nt_export: return compileExport(n);
                case nt_expression: return (void)compileExpression(n);
                case nt_function: {
                    if (!inInitFunction()) {
                        error(n, cm_err_function_scope, "'function' used outside of root scope");
                        return;
                    }

                    compileFunction(n);
                    return;
                }
                case nt_if: return compileIfStatement(n);
                case nt_loop: return compileLoop(n);
                case nt_return: return compileReturnStatement(n);
                case nt_scoped_block: return compileBlock(n);
                case nt_switch: return compileSwitchStatement(n);
                case nt_throw: return compileThrow(n);
                case nt_try: return compileTryBlock(n);
                case nt_type: {
                    if (!inInitFunction()) {
                        error(n, cm_err_type_scope, "'type' used outside of root scope");
                        return;
                    }

                    compileType(n);
                    return;
                }
                case nt_variable: {
                    if (n->body->tp == nt_object_decompositor) {
                        compileObjectDecompositor(n);
                        return;
                    }

                    compileVarDecl(n);
                    return;
                }
                default: break;
            }
        }
    };
};