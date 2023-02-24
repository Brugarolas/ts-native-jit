#pragma once
#include <tsn/bind/bind.h>
#include <tsn/bind/bind_type.hpp>
#include <tsn/bind/bind_func.hpp>
#include <tsn/common/Context.h>
#include <tsn/common/Module.h>

namespace tsn {
    namespace ffi {
        template <typename Cls>
        std::enable_if_t<std::is_fundamental_v<typename remove_all<Cls>::type>, PrimitiveTypeBinder<Cls>>
        bind(Context* ctx, const utils::String& name) {
            return PrimitiveTypeBinder<Cls>(nullptr, ctx->getFunctions(), ctx->getTypes(), name, "::" + name);
        }

        template <typename Cls>
        std::enable_if_t<std::is_fundamental_v<typename remove_all<Cls>::type>, PrimitiveTypeBinder<Cls>>
        bind(Module* mod, const utils::String& name) {
            Context* ctx = mod->getContext();
            return PrimitiveTypeBinder<Cls>(mod, ctx->getFunctions(), ctx->getTypes(), name, mod->getName() + "::" + name);
        }

        template <typename Cls>
        std::enable_if_t<!std::is_fundamental_v<typename remove_all<Cls>::type>, ObjectTypeBinder<Cls>>
        bind(Context* ctx, const utils::String& name) {
            return ObjectTypeBinder<Cls>(nullptr, ctx->getFunctions(), ctx->getTypes(), name, "::" + name);
        }

        template <typename Cls>
        std::enable_if_t<!std::is_fundamental_v<typename remove_all<Cls>::type>, ObjectTypeBinder<Cls>>
        bind(Module* mod, const utils::String& name) {
            Context* ctx = mod->getContext();
            return ObjectTypeBinder<Cls>(mod, ctx->getFunctions(), ctx->getTypes(), name, mod->getName() + "::" + name);
        }

        template <typename Ret, typename... Args>
        Function* bind(Context* ctx, const utils::String& name, Ret (*func)(Args...), access_modifier access) {
            return bind_function(nullptr, ctx->getFunctions(), ctx->getTypes(), name, func, access, nullptr);
        }

        template <typename Ret, typename... Args>
        Function* bind(Module* mod, const utils::String& name, Ret (*func)(Args...), access_modifier access) {
            Function* fn = bind_function(mod, mod->getContext()->getFunctions(), mod->getContext()->getTypes(), name, func, access, nullptr);
            if (fn) mod->addFunction(fn);
            return fn;
        }
    };
};