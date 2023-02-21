#include <tsn/compiler/Output.h>
#include <tsn/compiler/OutputBuilder.h>
#include <tsn/compiler/FunctionDef.h>
#include <tsn/compiler/Compiler.h>
#include <tsn/compiler/Parser.h>
#include <tsn/compiler/IR.h>
#include <tsn/compiler/Value.hpp>
#include <tsn/compiler/TemplateContext.h>
#include <tsn/common/Context.h>
#include <tsn/ffi/Function.h>
#include <tsn/ffi/FunctionRegistry.h>
#include <tsn/ffi/DataTypeRegistry.h>
#include <tsn/common/Module.h>
#include <tsn/io/Workspace.h>
#include <tsn/utils/SourceMap.h>
#include <tsn/utils/SourceLocation.h>
#include <tsn/utils/ModuleSource.h>

#include <utils/Array.hpp>
#include <utils/Buffer.hpp>

namespace tsn {
    namespace compiler {
        using namespace output;

        Output::Output(const script_metadata* meta, ModuleSource* src) {
            m_mod = nullptr;
            m_src = src;
            m_meta = meta;
        }

        Output::Output(OutputBuilder* in) {
            m_mod = in->getModule();
            m_src = m_mod->getSource();
            m_meta = m_mod->getInfo();

            const auto& funcs = in->getFuncs();
            for (u32 i = 0;i < funcs.size();i++) {
                if (!funcs[i]->getOutput()) continue;
                if (funcs[i]->getOutput()->isTemplate()) continue;
                
                const auto& code = funcs[i]->getCode();

                output::function f;
                f.func = funcs[i]->getOutput();
                f.icount = code.size();
                f.map = new SourceMap(in->getCompiler()->getScriptInfo()->modified_on);
                f.code = new output::instruction[code.size()];
                
                for (u32 c = 0;c < code.size();c++) {
                    auto& inst = f.code[c];
                    const auto& info = instruction_info(code[c].op);
                    inst.op = code[c].op;
                    
                    for (u8 o = 0;o < code[c].oCnt;o++) {
                        auto& v = inst.operands[o];
                        const auto& s = code[c].operands[o];
                        v.data_type = s.getType();
                        v.flags.is_reg = s.isReg();
                        v.flags.is_stack = s.isStack();
                        v.flags.is_func = s.isFunction();
                        v.flags.is_imm = s.isImm();

                        if (s.isReg()) v.value.reg_id = s.getRegId();
                        else if (s.isStack()) v.value.alloc_id = s.getStackAllocId();
                        else if (v.flags.is_imm) {
                            if (info.operands[o] == ot_fun) v.value.imm_u = s.getImm<FunctionDef*>()->getOutput()->getId();
                            else v.value.imm_u = s.getImm<u64>();
                        }
                    }

                    const SourceLocation& src = code[c].src;
                    f.map->add(src.getLine(), src.getCol(), src.getEndLocation().getOffset() - src.getOffset());
                }

                m_funcs.push(f);
            }
        }

        Output::~Output() {
            m_funcs.each([](output::function* f) {
                delete f->map;
                delete [] f->code;
            });
        }

        Module* Output::getModule() {
            return m_mod;
        }

        bool Output::serialize(utils::Buffer* out, Context* ctx) const {
            if (!out->write(m_mod->getId())) return false;
            if (!out->write(m_mod->getName())) return false;
            if (!out->write(m_mod->getPath())) return false;

            // Minus one because first function in IFunctionHolder is always null
            const auto funcs = m_mod->allFunctions();
            if (!out->write(funcs.size() - 1)) return false;
            for (u32 i = 1;i < funcs.size();i++) {
                if (!funcs[i]->serialize(out, ctx)) return false;
            }

            const auto& types = m_mod->allTypes();
            if (!out->write(types.size())) return false;
            for (u32 i = 0;i < types.size();i++) {
                if (!types[i]->serialize(out, ctx)) return false;
            }

            const auto& data = m_mod->getData();
            if (!out->write(data.size())) return false;
            for (u32 i = 0;i < data.size();i++) {
                if (!out->write(data[i].type ? data[i].type->getId() : 0)) return false;
                if (!out->write(data[i].size)) return false;
                if (!out->write(data[i].access)) return false;
                if (!out->write(data[i].name)) return false;
                if (!out->write(data[i].ptr, data[i].size)) return false;
            }

            if (!out->write(m_funcs.size())) return false;

            for (u32 i = 0;i < m_funcs.size();i++) {
                const auto& f = m_funcs[i];
                if (!out->write(f.func->getId())) return false;
                if (!out->write(f.icount)) return false;

                for (u32 c = 0;c < f.icount;c++) {
                    const auto& inst = f.code[c];
                    const auto& info = instruction_info(inst.op);
                    if (!out->write(inst.op)) return false;

                    for (u8 o = 0;o < info.operand_count;o++) {
                        if (!out->write(inst.operands[o].flags)) return false;
                        if (!out->write(inst.operands[o].data_type->getId())) return false;
                        if (!out->write(inst.operands[o].value)) return false;
                    }
                }

                if (!f.map->serialize(out, ctx)) return false;
            }

            return true;
        }

        bool Output::deserialize(utils::Buffer* in, Context* ctx) {
            utils::Array<proto_function> proto_funcs;
            utils::Array<proto_type> proto_types;
            utils::Array<TemplateContext*> tcontexts;
            ffi::FunctionRegistry* freg = ctx->getFunctions();
            ffi::DataTypeRegistry* treg = ctx->getTypes();

            u32 mid;
            if (!in->read(mid)) return false;
            utils::String name = in->readStr();
            if (name.size() == 0) return false;
            utils::String path = in->readStr();
            if (path.size() == 0) return false;
            
            m_mod = ctx->createModule(name, path, m_meta);
            if (m_mod->getId() != mid) return false;
            m_mod->setSrc(m_src);

            u32 count;
            // read functions
            if (!in->read(count)) return false;
            for (u32 i = 0;i < count;i++) {
                function_id id;
                if (!in->read(id)) return false;
                proto_funcs.push(proto_function());
                proto_function& pf = proto_funcs.last();
                pf.id = id;

                pf.name = in->readStr();
                if (pf.name.size() == 0) return false;
                pf.displayName = in->readStr();
                pf.fullyQualifiedName = in->readStr();

                if (!in->read(pf.access)) return false;
                if (!in->read(pf.signatureTypeId)) return false;
                if (!in->read(pf.isTemplate)) return false;
                if (!in->read(pf.isMethod)) return false;
                if (!pf.src.deserialize(in, ctx)) return false;
                
                if (pf.isMethod) {
                    if (!in->read(pf.baseOffset)) return false;
                }

                if (pf.isTemplate) {
                    pf.tctx = new TemplateContext();
                    if (!pf.tctx->deserialize(in, ctx)) {
                        delete pf.tctx;
                        pf.tctx = nullptr;
                        return false;
                    }

                    tcontexts.push(pf.tctx);
                    pf.tctx->getAST()->rehydrateSourceRefs(pf.tctx->getOrigin()->getSource());
                }
            }

            // read types
            if (!in->read(count)) return false;
            for (u32 i = 0;i < count;i++) {
                type_id id;
                if (!in->read(id)) return false;
                proto_types.push(proto_type());
                proto_type& pt = proto_types.last();
                pt.id = id;

                if (!in->read(pt.itype)) return false;

                pt.name = in->readStr();
                if (pt.name.size() == 0) return false;
                pt.fullyQualifiedName = in->readStr();
                if (pt.fullyQualifiedName.size() == 0) return false;
                if (!in->read(pt.info)) return false;
                if (!in->read(pt.access)) return false;
                if (!in->read(pt.destructorId)) return false;
                
                u32 pcount;
                if (!in->read(pcount)) return false;
                for (u32 p = 0;p < pcount;p++) {
                    pt.props.push(proto_type_prop());
                    proto_type_prop& prop = pt.props.last();

                    prop.name = in->readStr();
                    if (prop.name.size() == 0) return false;

                    if (!in->read(prop.access)) return false;
                    if (!in->read(prop.offset)) return false;
                    if (!in->read(prop.typeId)) return false;
                    if (!in->read(prop.flags)) return false;
                    if (!in->read(prop.getterId)) return false;
                    if (!in->read(prop.setterId)) return false;
                }

                u32 bcount;
                if (!in->read(bcount)) return false;
                pt.bases.reserve(bcount);
                for (u32 i = 0;i < bcount;i++) {
                    pt.bases.push(proto_type_base());
                    if (!in->read(pt.bases.last())) return false;
                }

                u32 mcount;
                if (!in->read(mcount)) return false;
                pt.methodIds.reserve(mcount);
                for (u32 i = 0;i < mcount;i++) {
                    function_id mid;
                    if (!in->read(mid)) return false;
                    pt.methodIds.push(mid);
                }

                if (!in->read(pt.templateBaseId)) return false;
                u32 tacount;
                if (!in->read(tacount)) return false;
                for (u32 i = 0;i < tacount;i++) {
                    type_id tid;
                    if (!in->read(tid)) return false;
                    pt.templateArgIds.push(tid);
                }

                switch (pt.itype) {
                    case ffi::dti_plain: break;
                    case ffi::dti_function: {
                        if (!in->read(pt.returnTypeId)) return false;
                        if (!in->read(pt.returnsPointer)) return false;
                        u32 acount;
                        if (!in->read(acount)) return false;
                        for (u32 a = 0;a < acount;a++) {
                            proto_type_arg arg;
                            if (!in->read(arg.argType)) return false;
                            if (!in->read(arg.dataTypeId)) return false;
                            pt.args.push(arg);
                        }
                        break;
                    }
                    case ffi::dti_template: {
                        pt.tctx = new TemplateContext();
                        if (!pt.tctx->deserialize(in, ctx)) {
                            delete pt.tctx;
                            pt.tctx = nullptr;
                            return false;
                        }
                    
                        tcontexts.push(pt.tctx);
                        pt.tctx->getAST()->rehydrateSourceRefs(m_mod->getSource());
                        break;
                    }
                    case ffi::dti_alias: {
                        if (!in->read(pt.aliasTypeId)) return false;
                        break;
                    }
                    case ffi::dti_class: break;
                }
            }

            if (!generateTypesAndFunctions(proto_funcs, proto_types, freg, treg)) return false;

            // resolve template context data
            for (u32 i = 0;i < tcontexts.size();i++) {
                if (!tcontexts[i]->resolveReferences(ctx)) return false;
            }

            // read data
            if (!in->read(count)) return false;
            for (u32 i = 0;i < count;i++) {
                module_data md;
                type_id tid;
                if (!in->read(tid)) return false;
                md.type = tid == 0 ? nullptr : treg->getType(tid);

                if (!in->read(md.size)) return false;
                if (!in->read(md.access)) return false;
                md.name = in->readStr();
                if (md.name.size() == 0) return false;

                if (md.type) m_mod->addData(md.name, md.type, md.access);
                else m_mod->addData(md.name, md.size);
                
                if (!in->read(m_mod->m_data[i].ptr, md.size)) return false;
            }

            if (!in->read(count)) return false;
            for (u32 i = 0;i < count;i++) {
                output::function f;
                u32 fid;
                if (!in->read(fid)) return false;
                f.func = freg->getFunction(fid);
                if (!f.func) return false;

                if (!in->read(f.icount)) return false;
                f.code = new output::instruction[f.icount];

                for (u32 c = 0;c < f.icount;c++) {
                    auto& inst = f.code[c];
                    inst.operands[0].data_type = nullptr;
                    inst.operands[0].value.imm_u = 0;
                    inst.operands[0].flags = { 0 };
                    inst.operands[1].data_type = nullptr;
                    inst.operands[1].value.imm_u = 0;
                    inst.operands[1].flags = { 0 };
                    inst.operands[2].data_type = nullptr;
                    inst.operands[2].value.imm_u = 0;
                    inst.operands[2].flags = { 0 };
                    if (!in->read(inst.op)) {
                        delete [] f.code;
                        return false;
                    }

                    const auto& info = instruction_info(inst.op);
                    for (u8 o = 0;o < info.operand_count;o++) {
                        if (!in->read(inst.operands[o].flags)) {
                            delete [] f.code;
                            return false;
                        }

                        type_id tid;
                        if (!in->read(tid)) {
                            delete [] f.code;
                            return false;
                        }

                        inst.operands[o].data_type = treg->getType(tid);
                        if (!inst.operands[o].data_type) {
                            delete [] f.code;
                            return false;
                        }

                        if (!in->read(inst.operands[o].value)) {
                            delete [] f.code;
                            return false;
                        }
                    }
                }
            
                f.map = new SourceMap();
                if (!f.map->deserialize(in, ctx)) {
                    delete [] f.code;
                    delete f.map;
                    return false;
                }

                m_funcs.push(f);
            }

            return true;
        }

        bool Output::generateTypesAndFunctions(
            utils::Array<proto_function>& funcs,
            utils::Array<proto_type>& types,
            ffi::FunctionRegistry* freg,
            ffi::DataTypeRegistry* treg
        ) {
            utils::Array<ffi::Function*> ofuncs(funcs.size());
            utils::Array<ffi::DataType*> otypes(types.size());

            // Generate functions without signatures
            for (auto& pf : funcs) {
                ffi::Function* f = nullptr;
                if (pf.isMethod) {
                    if (pf.isTemplate) f = new ffi::TemplateMethod(pf.name, "", pf.access, pf.baseOffset, pf.tctx);
                    else f = new ffi::Method(pf.name, "", nullptr, pf.access, nullptr, nullptr, pf.baseOffset);
                } else {
                    if (pf.isTemplate) f = new ffi::TemplateFunction(pf.name, "", pf.access, pf.tctx);
                    else f = new ffi::Function(pf.name, "", nullptr, pf.access, nullptr, nullptr);
                }

                f->m_fullyQualifiedName = pf.fullyQualifiedName;
                f->m_displayName = pf.displayName;
                f->m_id = pf.id;
                ofuncs.push(f);
                freg->registerFunction(f);
                m_mod->addFunction(f);
            }

            // Generate types without properties, bases, template info, or derived info
            for (auto& pt : types) {
                ffi::DataType* tp = nullptr;
                switch (pt.itype) {
                    case ffi::dti_plain: { tp = new ffi::DataType(); break; }
                    case ffi::dti_function: { tp = new ffi::FunctionType(); break; }
                    case ffi::dti_template: { tp = new ffi::TemplateType(); break; }
                    case ffi::dti_alias: { tp = new ffi::AliasType(); break; }
                    case ffi::dti_class: { tp = new ffi::ClassType(); break; }
                }

                tp->m_id = pt.id;
                tp->m_name = pt.name;
                tp->m_fullyQualifiedName = pt.fullyQualifiedName;
                tp->m_info = pt.info;
                tp->m_access = pt.access;
                tp->m_destructor = pt.destructorId == 0 ? nullptr : freg->getFunction(pt.destructorId);

                for (auto mid : pt.methodIds) {
                    tp->m_methods.push(freg->getFunction(mid));
                }

                treg->addForeignType(tp);
                m_mod->addForeignType(tp);
                otypes.push(tp);
            }

            // Add signatures to functions
            for (u32 i = 0;i < funcs.size();i++) {
                proto_function& pf = funcs[i];
                ffi::Function* f = ofuncs[i];

                f->m_signature = pf.signatureTypeId == 0 ? nullptr : (ffi::FunctionType*)treg->getType(pf.signatureTypeId);
            }

            // Add properties, bases, template info, derived info to types
            for (u32 i = 0;i < types.size();i++) {
                proto_type& pt = types[i];
                ffi::DataType* tp = otypes[i];

                for (u32 p = 0;p < pt.props.size();p++) {
                    proto_type_prop& pp = pt.props[p];
                    tp->m_properties.push(ffi::type_property());
                    ffi::type_property& prop = tp->m_properties.last();

                    prop.name = pp.name;
                    prop.flags = pp.flags;
                    prop.access = pp.access;
                    prop.offset = pp.offset;
                    prop.type = treg->getType(pp.typeId);
                    prop.getter = pp.getterId == 0 ? nullptr : freg->getFunction(pp.getterId);
                    prop.setter = pp.setterId == 0 ? nullptr : freg->getFunction(pp.setterId);
                }

                for (u32 b = 0;b < pt.bases.size();b++) {
                    proto_type_base& pb = pt.bases[b];
                    tp->m_bases.push(ffi::type_base());
                    ffi::type_base& base = tp->m_bases.last();

                    base.access = pb.access;
                    base.offset = pb.offset;
                    base.type = treg->getType(pb.typeId);
                }

                if (pt.templateBaseId != 0) {
                    tp->m_templateBase = (ffi::TemplateType*)treg->getType(pt.templateBaseId);
                }

                for (u32 a = 0;a < pt.templateArgIds.size();a++) {
                    tp->m_templateArgs.push(treg->getType(pt.templateArgIds[a]));
                }

                switch (pt.itype) {
                    case ffi::dti_plain: break;
                    case ffi::dti_function: {
                        ffi::FunctionType* ft = (ffi::FunctionType*)tp;
                        ft->m_returnType = pt.returnTypeId == 0 ? nullptr : treg->getType(pt.returnTypeId);
                        ft->m_returnsPointer = pt.returnsPointer;
                        
                        for (u32 a = 0;a < pt.args.size();a++) {
                            proto_type_arg& parg = pt.args[a];
                            ft->m_args.push(ffi::function_argument());
                            ffi::function_argument& arg = ft->m_args.last();

                            arg.argType = parg.argType;
                            arg.dataType = treg->getType(parg.dataTypeId);
                        }
                        break;
                    }
                    case ffi::dti_template: {
                        ffi::TemplateType* tt = (ffi::TemplateType*)tp;
                        tt->m_data = pt.tctx;
                        break;
                    }
                    case ffi::dti_alias: {
                        ffi::AliasType* at = (ffi::AliasType*)tp;
                        at->m_ref = treg->getType(pt.aliasTypeId);
                        break;
                    }
                    case ffi::dti_class: break;
                }
            }

            return true;
        }
    };
};