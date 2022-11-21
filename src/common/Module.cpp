#include <gs/common/Module.h>
#include <gs/common/DataType.h>
#include <utils/Array.hpp>

namespace gs {
    Module::Module(Context* ctx, const utils::String& name) : IContextual(ctx), m_name(name) {
        m_id = std::hash<utils::String>()(name);
    }

    Module::~Module() {}

    
    u32 Module::getId() const {
        return m_id;
    }

    const utils::String& Module::getName() const {
        return m_name;
    }

    u32 Module::getDataSlotCount() const {
        return m_data.size();
    }

    const module_data& Module::getDataInfo(u32 slot) const {
        return m_data[slot];
    }

    void Module::setDataAccess(u32 slot, access_modifier access) {
        m_data[slot].access = access;
    }

    Object Module::getData(u32 slot) {
        return Object(m_ctx, false, m_data[slot].type, m_data[slot].ptr);
    }
    
    const utils::Array<module_data>& Module::getData() const {
        return m_data;
    }

    u32 Module::addData(const utils::String& name, ffi::DataType* tp, access_modifier access) {
        u32 sz = tp->getInfo().size;
        
        m_data.push({
            new u8[tp->getInfo().size],
            tp,
            access,
            name
        });

        return m_data.size() - 1;
    }
};