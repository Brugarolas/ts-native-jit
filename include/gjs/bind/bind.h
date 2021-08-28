#pragma once
#include <gjs/common/types.h>
#include <gjs/common/function_signature.h>
#include <gjs/builtin/builtin.h>
#include <gjs/util/template_utils.hpp>
#include <gjs/util/robin_hood.h>
#include <dyncall.h>

#include <vector>
#include <tuple>
#include <string>

// If a function is overloaded, this can help select it to pass to a binding function
// Usage:
//    ctx->bind(FUNC_PTR(some_func, return_type, arg_type0, arg_type1, ... arg_typeN), "some_func");
#define FUNC_PTR(func, ret, ...) ((ret(*)(__VA_ARGS__))func)

// If a class method is overloaded, this can help select it to pass to a binding function
// Usage:
//    ctx->bind(FUNC_PTR(some_class, some_func, return_type, arg_type0, arg_type1, ... arg_typeN), "some_func");
#define METHOD_PTR(cls, method, ret, ...) ((ret(cls::*)(__VA_ARGS__))&cls::method)

// If a const class method is overloaded, this can help select it to pass to a binding function
// Usage:
//    ctx->bind(FUNC_PTR(some_class, some_func, return_type, arg_type0, arg_type1, ... arg_typeN), "some_func");
#define CONST_METHOD_PTR(cls, method, ret, ...) ((ret(cls::*)(__VA_ARGS__) const)&cls::method)

namespace gjs {
    class script_type;
    class script_module;
    class type_manager;

    class bind_exception : public std::exception {
        public:
            bind_exception(const std::string& _text) : text(_text) { }
            ~bind_exception() { }

            virtual char const* what() const { return text.c_str(); }

            std::string text;
    };

    //
    // Function call wrappers
    //
    namespace bind {
        // SRV wrapper (Struct-Return-Value)
        // Wraps call in a function which copy-constructs the return value of the
        // callee with the placement new operator, constructing the result in an
        // output parameter.
        template <typename Ret, typename... Args>
        void srv_wrapper(Ret* out, Ret (*f)(Args...), Args... args) {
            new (out) Ret(f(args...));
        }

        // Non-void class method wrappers
        // Wraps call to class method in a function which accepts a pointer to the class
        // instance to call the method on.
        // Only visible to class methods with non-void return.

        // Non-const methods
        template <typename Ret, typename Cls, typename... Args>
        typename std::enable_if<!std::is_same<Ret, void>::value, Ret>::type
            call_class_method(Ret(Cls::*method)(Args...), Cls* self, Args... args) {
            return (*self.*method)(args...);
        }

        // Const methods
        template <typename Ret, typename Cls, typename... Args>
        typename std::enable_if<!std::is_same<Ret, void>::value, Ret>::type
            call_const_class_method(Ret(Cls::*method)(Args...) const, Cls* self, Args... args) {
            return (*self.*method)(args...);
        }

        // Void class method wrappers
        // Wraps call to class method in a function which accepts a pointer to the class
        // instance to call the method on.
        // Only visible to class methods with void return.

        // Non-const methods
        template <typename Ret, typename Cls, typename... Args>
        typename std::enable_if<std::is_same<Ret, void>::value, Ret>::type
            call_class_method(Ret(Cls::*method)(Args...), Cls* self, Args... args) {
            (*self.*method)(args...);
        }

        // Const methods
        template <typename Ret, typename Cls, typename... Args>
        typename std::enable_if<std::is_same<Ret, void>::value, Ret>::type
            call_const_class_method(Ret(Cls::*method)(Args...) const, Cls* self, Args... args) {
            (*self.*method)(args...);
        }

        // Class constructor wrapper
        // Wraps call to class constructors in a function that forces the class to be
        // constructed in an output parameter using the placement new operator.
        template <typename Cls, typename... Args>
        void construct_object(Cls* mem, Args... args) {
            new (mem) Cls(args...);
        }

        // Class copy constructor wrapper
        // Wraps call to class constructors in a function that forces the class to be
        // constructed in an output parameter using the placement new operator.
        template <typename Cls>
        void copy_construct_object(void* dest, void* src, size_t sz) {
            new ((Cls*)dest) Cls(*(Cls*)src);
        }

        // Class destructor wrapper
        // Wraps call to class destructor.
        template <typename Cls>
        void destruct_object(Cls* obj) {
            obj->~Cls();
        }
    };

    //
    // Wrapped class / function types
    //
    namespace bind {
        typedef void (*pass_ret_func)(void* /*dest*/, void* /*src*/, size_t /*size*/);
        void trivial_copy(void* dest, void* src, size_t sz);

        struct wrapped_function {
            wrapped_function(script_type* ret, std::vector<script_type*> args, const std::string& _name, bool anonymous);

            // Name of the function
            std::string name;

            // Return type of the function
            script_type* return_type;

            // Whether or not each argument is a pointer
            std::vector<bool> arg_is_ptr;

            // Argument types
            std::vector<script_type*> arg_types;

            // Whether or not the function is a static class method
            bool is_static_method;

            // Whether or not the function is anonymous (not added to any module or context)
            bool is_anonymous;

            // whether or not to pass this obj when the method is static
            // (Useful for artificially extending types)
            bool pass_this;

            // Whether or not the return value is a pointer
            bool ret_is_ptr;

            // address of the host function to be called
            void* func_ptr;

            // Address of the struct-return-value function wrapper
            // For class methods, the signature of this function is:
            //     void (return_type*, call_method_func, func_ptr, this_ptr, arg_types...)
            // For global functions, the signature of this function is:
            //     void (return_type*, func_ptr, arg_types...)
            void* srv_wrapper_func;

            // func_ptr, self, method args...
            // Address of the call_class_method/call_const_class_method function wrapper
            // The signature of this function is return_type
            void* call_method_func;

            virtual void call(DCCallVM* call, void* ret, void** args) = 0;
        };

        struct wrapped_class {
            struct property {
                property(wrapped_function* g, wrapped_function* s, script_type* t, u64 o, u8 f);

                wrapped_function* getter;
                wrapped_function* setter;
                script_type* type;
                u64 offset;
                u8 flags;
            };

            wrapped_class(const std::string& _name, const std::string& _internal_name, size_t _size);

            ~wrapped_class();

            std::string name;
            std::string internal_name;
            bool is_pod;
            bool trivially_copyable;
            pass_ret_func pass_ret;
            std::vector<wrapped_function*> methods;
            robin_hood::unordered_map<std::string, property*> properties;
            wrapped_function* dtor;
            script_type* type;
            size_t size;
        };

        template <typename Ret, typename... Args>
        struct global_function : wrapped_function {
            typedef Ret (*func_type)(Args...);
            typedef std::tuple_size<std::tuple<Args...>> arg_count;

            global_function(type_manager* tpm, func_type f, const std::string& name, bool anonymous);
            virtual void call(DCCallVM* call, void* ret, void** args);
        };

        template <typename Ret, typename Cls, typename... Args>
        struct class_method : wrapped_function {
            public:
                typedef Ret (Cls::*method_type)(Args...);
                typedef std::tuple_size<std::tuple<Args...>> ac;
                typedef Ret (*func_type)(method_type, Cls*, Args...);

                func_type wrapper;
                
                class_method(type_manager* tpm, method_type f, const std::string& name, bool anonymous);
                virtual void call(DCCallVM* call, void* ret, void** args);
        };

        template <typename Ret, typename Cls, typename... Args>
        struct const_class_method : wrapped_function {
            public:
                typedef Ret (Cls::*method_type)(Args...) const;
                typedef std::tuple_size<std::tuple<Args...>> ac;
                typedef Ret (*func_type)(method_type, Cls*, Args...);

                func_type wrapper;
                
                const_class_method(type_manager* tpm, method_type f, const std::string& name, bool anonymous);
                virtual void call(DCCallVM* call, void* ret, void** args);
        };
    };

    //
    // Function wrappers
    //
    namespace bind {
        template <typename Ret, typename... Args>
        wrapped_function* wrap(type_manager* tpm, const std::string& name, Ret(*func)(Args...), bool anonymous = false);

        template <typename Ret, typename Cls, typename... Args>
        wrapped_function* wrap(type_manager* tpm, const std::string& name, Ret(Cls::*func)(Args...), bool anonymous = false);

        template <typename Ret, typename Cls, typename... Args>
        wrapped_function* wrap(type_manager* tpm, const std::string& name, Ret(Cls::*func)(Args...) const, bool anonymous = false);

        template <typename Cls, typename... Args>
        wrapped_function* wrap_constructor(type_manager* tpm, const std::string& name);

        template <typename Cls>
        wrapped_function* wrap_destructor(type_manager* tpm, const std::string& name);
    };

    //
    // Class wrappers
    //
    namespace bind {
        enum property_flags {
            pf_none             = 0b00000000,
            pf_read_only        = 0b00000001,
            pf_write_only       = 0b00000010,
            pf_pointer          = 0b00000100,
            pf_static           = 0b00001000
        };

        template <typename Cls>
        struct wrap_class : wrapped_class {
            wrap_class(type_manager* tpm, const std::string& name);

            template <typename... Args, std::enable_if_t<sizeof...(Args) != 0, int> = 0>
            wrap_class& constructor();

            template <typename... Args, std::enable_if_t<sizeof...(Args) == 0, int> = 0>
            wrap_class& constructor();

            // non-const methods
            template <typename Ret, typename... Args>
            wrap_class& method(const std::string& _name, Ret(Cls::*func)(Args...));

            // const methods
            template <typename Ret, typename... Args>
            wrap_class& method(const std::string& _name, Ret(Cls::*func)(Args...) const);

            // static methods
            template <typename Ret, typename... Args>
            wrap_class& method(const std::string& _name, Ret(*func)(Args...));

            // static method which can have 'this' obj passed to it
            template <typename Ret, typename... Args>
            wrap_class& method(const std::string& _name, Ret(*func)(Args...), bool pass_this);

            // normal member
            template <typename T>
            wrap_class& prop(const std::string& _name, T Cls::*member, u8 flags = property_flags::pf_none);

            // static member
            template <typename T>
            wrap_class& prop(const std::string& _name, T *member, u8 flags = property_flags::pf_none);

            // getter, setter member
            template <typename T>
            wrap_class& prop(const std::string& _name, T(Cls::*getter)(), T(Cls::*setter)(T), u8 flags = property_flags::pf_none);

            // const getter, setter member
            template <typename T>
            wrap_class& prop(const std::string& _name, T(Cls::*getter)() const, T(Cls::*setter)(T), u8 flags = property_flags::pf_none);

            // read only member with getter
            template <typename T>
            wrap_class& prop(const std::string& _name, T(Cls::*getter)(), u8 flags = property_flags::pf_none);

            // read only member with const getter
            template <typename T>
            wrap_class& prop(const std::string& _name, T(Cls::*getter)() const, u8 flags = property_flags::pf_none);

            script_type* finalize(script_module* mod);

            type_manager* types;
        };

        template <typename prim>
        struct pseudo_class : wrapped_class {
            pseudo_class(type_manager* tpm, const std::string& name);

            template <typename Ret, typename... Args>
            pseudo_class& method(const std::string& _name, Ret(*func)(prim, Args...));

            script_type* finalize(script_module* mod);

            type_manager* types;
        };
    };

    //
    // Helpers for calling host functions from the VM
    //
    namespace bind {        
        template <typename T>
        inline void pass_arg(DCCallVM* call, std::enable_if_t<std::is_pointer_v<T> || std::is_reference_v<T>, void*> p);

        template <typename T>
        inline void pass_arg(DCCallVM* call, std::enable_if_t<is_callback<T>::value, void*> p);

        template <typename T>
        inline void pass_arg(DCCallVM* call, std::enable_if_t<std::is_class_v<T> && !is_callback<T>::value, void*> p);

        template <typename T, typename... Rest>
        void _pass_arg_wrapper(u16 i, DCCallVM* call, void** params);

        template <typename T>
        void do_call(DCCallVM* call, std::enable_if_t<!std::is_pointer_v<T>, T>* ret, void* func);

        template <typename T>
        inline void do_call(DCCallVM* call, std::enable_if_t<std::is_pointer_v<T>, T>* ret, void* func);

        template <> void do_call<void>(DCCallVM* call, void* ret, void* func);

        #define dc_func_simp(tp, cfunc) template <> void do_call<tp>(DCCallVM* call, tp* ret, void* func);
        dc_func_simp(f32, dcCallFloat);
        dc_func_simp(f64, dcCallDouble);
        dc_func_simp(bool, dcCallBool);
        dc_func_simp(u8, dcCallChar);
        dc_func_simp(i8, dcCallChar);
        dc_func_simp(u16, dcCallShort);
        dc_func_simp(i16, dcCallShort);
        dc_func_simp(u32, dcCallInt);
        dc_func_simp(i32, dcCallInt);
        dc_func_simp(u64, dcCallLongLong);
        dc_func_simp(i64, dcCallLongLong);
        #undef dc_func_simp
    };
};

#include <gjs/bind/bind.inl>