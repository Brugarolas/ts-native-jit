#pragma once
#include <common/types.h>

namespace gjs {
    class script_context;
    class script_type;
    struct subtype_t;

    void set_builtin_context(script_context* ctx);
    void init_context(script_context* ctx);
    
    void* script_allocate(u64 size);
    void script_free(void* ptr);
    void script_copymem(void* to, void* from, u64 size);

    class script_array {
        public:
            script_array(script_type* type);
            ~script_array();

            void push(subtype_t* elem);
            subtype_t* operator[](u32 idx);
            u32 length();

        protected:
            u64 m_size;
            u32 m_count;
            u32 m_capacity;
            u8* m_data;
            script_type* m_type;
    };
};