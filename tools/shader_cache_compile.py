"""Run pinned hlslkit with FXC definitions matching the runtime's empty macros."""

from __future__ import annotations

from hlslkit import compile_shaders as compiler

_compile_shader = compiler.compile_shader
_validate_shader_inputs = compiler.validate_shader_inputs


def validate_shader_inputs(fxc_path, shader_file, output_dir, defines, shader_dir):
    # Validate empty definitions as bare names; all other upstream checks remain.
    validation_defines = [value[:-2] if value.endswith("= ") else value for value in defines]
    return _validate_shader_inputs(fxc_path, shader_file, output_dir, validation_defines, shader_dir)


def compile_shader(fxc_path, shader_file, shader_type, entry, defines, *args, **kwargs):
    # FXC consumes the next argument for NAME=; one space is an empty replacement.
    fxc_defines = [value + " " if value.endswith("=") else value for value in defines]
    return _compile_shader(fxc_path, shader_file, shader_type, entry, fxc_defines, *args, **kwargs)


def main() -> int:
    compiler.validate_shader_inputs = validate_shader_inputs
    compiler.compile_shader = compile_shader
    return compiler.main()


if __name__ == "__main__":
    raise SystemExit(main())
