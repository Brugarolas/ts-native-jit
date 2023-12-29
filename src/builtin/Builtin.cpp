#include <tsn/builtin/Builtin.h>
#include <tsn/common/Context.h>
#include <tsn/common/Module.h>
#include <tsn/ffi/DataTypeRegistry.h>
#include <tsn/ffi/Closure.h>
#include <tsn/bind/bind.hpp>

using namespace tsn::ffi;

namespace tsn {
    template <typename T>
    void BindNumberType(Context* ctx, const char* name) {
        auto b = bind<T>(ctx, name);
        b.finalize();
    }

    template <typename T>
    void ExtendNumberType(Context* ctx) {
        auto tp = extend<T>(ctx);

        tp.method("toString", +[](T* self) {
            if constexpr (std::is_floating_point_v<T>) {
                return utils::String::Format("%f", *self);
            } else if constexpr (std::is_integral_v<T>) {
                if constexpr (std::is_unsigned_v<T>) {
                    return utils::String::Format("%llu", (u64)*self);
                } else {
                    return utils::String::Format("%lli", (i64)*self);
                }
            }
        }, public_access);
    }

    i32 print(const utils::String& str) {
        return printf(str.c_str());
    }

    void BindString(Context* ctx) {
        auto b = bind<utils::String>(ctx, "string");

        b.ctor(access_modifier::public_access);
        b.ctor<const utils::String&>(access_modifier::public_access);
        b.ctor<void*, u32>(access_modifier::trusted_access);
        b.dtor(access_modifier::public_access);
        
        b.finalize();
    }

    void AddBuiltInBindings(Context* ctx) {
        BindNumberType<i8 >(ctx, "i8" );
        BindNumberType<i16>(ctx, "i16");
        BindNumberType<i32>(ctx, "i32");
        BindNumberType<i64>(ctx, "i64");
        BindNumberType<u8 >(ctx, "u8" );
        BindNumberType<u16>(ctx, "u16");
        BindNumberType<u32>(ctx, "u32");
        BindNumberType<u64>(ctx, "u64");
        BindNumberType<f32>(ctx, "f32");
        BindNumberType<f64>(ctx, "f64");

        auto v  = bind<void>(ctx, "void").finalize();

        auto vp = bind<void*>(ctx, "data");
        type_meta& vpi = vp.info();
        vpi.is_primitive = 1;
        vpi.is_unsigned = 1;
        vpi.is_integral = 1;
        vp.access(trusted_access).finalize();

        auto n = bind<null_t>(ctx, "null_t").finalize();

        auto b = bind<bool>(ctx, "boolean").finalize();
        auto p = bind<poison_t>(ctx, "$poison").finalize();
        auto ectx = bind<ExecutionContext>(ctx, "$exec").dtor(tsn::private_access).finalize();
        
        auto cb = bind<Closure>(ctx, "$closure");
        cb.ctor<const Closure&>(public_access);
        cb.dtor(private_access);
        cb.finalize();

        auto cbr = bind<ClosureRef>(ctx, "$closure_ref");
        cbr.ctor<const ClosureRef&>(public_access);
        cbr.ctor<Closure*>(public_access);
        cbr.dtor(public_access);
        cbr.finalize();

        BindString(ctx);
        BindMemory(ctx);

        bind(ctx, "print", print);

        // pointer compilation needs the above
        ctx->getTypes()->updateCachedTypes();

        Module* mp = ctx->getModule("trusted/pointer");
        if (mp) {
            DataType* ptr = mp->allTypes().find([](const DataType* t) { return t->getName() == "Pointer"; });
            if (ptr) ctx->getGlobal()->addForeignType(ptr);
        }

        // array compilation needs pointer
        ctx->getTypes()->updateCachedTypes();

        Module* ma = ctx->getModule("trusted/array");
        if (ma) {
            DataType* arr = ma->allTypes().find([](const DataType* t) { return t->getName() == "Array"; });
            if (arr) ctx->getGlobal()->addForeignType(arr);
        }

        auto ev = extend<void*>(ctx);
        ev.method("toString", +[](void** self) {
            return utils::String::Format("0x%X", reinterpret_cast<u64>(*self));
        }, public_access);

        ExtendNumberType<i8 >(ctx);
        ExtendNumberType<i16>(ctx);
        ExtendNumberType<i32>(ctx);
        ExtendNumberType<i64>(ctx);
        ExtendNumberType<u8 >(ctx);
        ExtendNumberType<u16>(ctx);
        ExtendNumberType<u32>(ctx);
        ExtendNumberType<u64>(ctx);
        ExtendNumberType<f32>(ctx);
        ExtendNumberType<f64>(ctx);
    }
};