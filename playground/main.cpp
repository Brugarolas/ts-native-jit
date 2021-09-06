#include <gjs/gjs.h>
#include <stdio.h>

using namespace gjs;

void remove_unused_regs_pass (script_context* ctx, compilation_output& in, u16 f) {
    compilation_output::ir_code& code = in.funcs[f].code;
    u64 csz = code.size();
    for (u64 c = 0;c < code.size() || c == u64(-1);c++) {
        if (compile::is_assignment(code[c]) && code[c].op != compile::operation::call) {
            u32 assigned_reg = code[c].operands[0].reg_id();

            bool used = false;
            for (u64 c1 = c + 1;c1 < code.size() && !used;c1++) {
                auto& i = code[c1];
                for (u8 o = 0;o < 3 && !used;o++) {
                    used = i.operands[o].valid() && !i.operands[o].is_imm() && i.operands[o].reg_id() == assigned_reg;
                }
            }

            if (!used) {
                in.funcs[f].erase(c);
                c--;
            }
        }
    }

    if (csz > code.size()) remove_unused_regs_pass(ctx, in, f);
}

// todo: destruct captured variables

int main(int arg_count, const char** args) {
    basic_malloc_allocator alloc;
    vm_backend be(&alloc, 8 * 1024 * 1024, 8 * 1024 * 1024);
    script_context ctx(&be);

    be.commit_bindings();
    // be.log_ir(true);
    ctx.io()->set_cwd_from_args(arg_count, args);
    // ctx.compiler()->add_ir_step(remove_unused_regs_pass);
    // ctx.compiler()->add_ir_step(debug_ir_step, false);
    // ctx.compiler()->add_ir_step(debug_ir_step, true);

    script_module* mod = ctx.resolve("test");
    if (!mod) {
        print_log(&ctx);
        return -1;
    }
    // print_code(&be);
    // be.log_instructions(true);

    mod->init();

    i32 someVal = 0;
    mod->function("set_some_value")->call(nullptr, [&someVal](i32 val) {
        someVal = val;
    });

    printf("someVal was set to %d.\n", someVal);

    return 0;
}