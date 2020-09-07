#include <errors.h>
#include <stdarg.h>

namespace gjs {
	namespace error {
		static const char* error_fmts[] = {
			"No Error",
			"", // parse errors
			"Expected identifier",
			"Unexpected identifier '%s'",
			"Expected type identifier",
			"Expected '%c'",
			"Unexpected token '%s'",
			"Expected keyword '%s'",
			"Unexpected keyword '%s'",
			"Expected operator",
			"Expected expression",
			"Expected identifier, member expression, or index expression",
			"Expected identifier or member expression",
			"Expected class property or method",
			"Unexpected end of file while parsing %s",
			"", // end parse errors
			"", // end compile errors
			"No source code to compile",
			"Encountered invalid AST node. This is not a user error",
			"Type '%s' has no property '%s'",
			"Type '%s' has no method '%s' that matches the argument list '%s' and return type '%s'",
			"No function '%s' matching argument list '%s' and return type '%s' was found",
			"Reference to method '%s' of type '%s' is ambiguous with the argument list '%s' and return type '%s'",
			"Reference to function '%s' is ambiguous with the argument list '%s' and return type '%s'",
			"" // end compile errors
		};

		const char* format_str(ecode c) {
			return error_fmts[(u16)c];
		}

		exception::exception(ecode _code, source_ref at, ...) : code(_code), src(at) {
			va_list l;
			va_start(l, at);
			char out[1024] = { 0 };
			vsnprintf(out, 1024, error_fmts[(u16)code], l);
			va_end(l);
			message = out;
		}
	};
};