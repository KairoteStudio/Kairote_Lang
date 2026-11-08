const std = @import("std");

pub const minimum_zig_version = "0.16.0";

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const common_includes = &.{
        b.path("src"),
        b.path("src/Core"),
        b.path("src/Tools"),
        b.path("src/Bytecode"),
        b.path("Shared"),
        b.path("StubInclude"),
        b.path("vm"),
    };

    const c_flags = &.{
        "-Wall",
        "-Wextra",
        "-Werror=unused-variable",
        "-Werror=unused-function",
        "-Werror=unused-parameter",
        "-Werror=unused-but-set-variable",
        "-Werror=implicit-function-declaration",
        "-Wformat=2",
        "-Wshadow",
    };

    const krtc_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .root_source_file = null,
    });

    const krtc = b.addExecutable(.{
        .name = "KrtC",
        .root_module = krtc_module,
    });

    krtc_module.addCSourceFiles(.{
        .files = &.{
            "src/Core/Memory/Allocator.c",
            "src/Core/Memory/Arena.c",
            "src/Core/Memory/LeakDetector.c",
            "src/Core/Memory/SmartPtr.c",
            "src/Core/Platform/ThreadPool.c",
            "src/Core/Utils/Error.c",
            "src/Core/Utils/KrtString.c",
            "src/Core/Utils/KrtTime.c",
            "src/Core/Utils/Logger.c",
            "src/Core/Utils/OutputCache.c",
            "src/Core/Utils/Path.c",
            "src/Core/Utils/StackCalculator.c",
            "src/Compiler/Driver/ArkLinkIntegration.c",
            "src/Compiler/Driver/Compiler.c",
            "src/Compiler/Driver/ConfigManager.c",
            "src/Compiler/Driver/ConsoleUtils.c",
            "src/Compiler/Driver/ParallelCompiler.c",
            "src/Compiler/Driver/ParallelCompilerLink.c",
            "src/Compiler/Driver/Preprocessor.c",
            "src/Compiler/Driver/Project.c",
            "src/Compiler/Driver/ProjectKrt.c",
            "src/Compiler/Driver/TaskManager.c",
            "src/Compiler/Frontend/Lexer/Tokenizer.c",
            "src/Compiler/Frontend/Parser/Ast.c",
            "src/Compiler/Frontend/Parser/Parser.c",
            "src/Compiler/Frontend/Parser/ParserAdvanced.c",
            "src/Compiler/Frontend/Parser/ParserBase.c",
            "src/Compiler/Frontend/Parser/ParserExpression.c",
            "src/Compiler/Frontend/Parser/ParserStatement.c",
            "src/Compiler/Frontend/CompilerError.c",
            "src/Compiler/Frontend/Semantic/Generics.c",
            "src/Compiler/Frontend/Semantic/NameMangling.c",
            "src/Compiler/Frontend/Semantic/SemanticAnalyzer.c",
            "src/Compiler/Frontend/Semantic/SymbolTable.c",
            "src/Compiler/Middle/Codegen/IrGen.c",
            "src/Compiler/Middle/Ir/Ir.c",
            "src/Compiler/Middle/Ir/IrMemory.c",
            "src/Compiler/Middle/Ir/IrOptimizer.c",
            "src/Compiler/Middle/Ir/IrParamTable.c",
            "src/Compiler/Middle/Ir/IrSsa.c",
            "src/Compiler/Middle/Ir/IrType.c",
            "src/Compiler/Backend/Kro/KroCodegen.c",
            "src/Compiler/Backend/Vm/VmCodegen.c",
            "src/Compiler/Backend/X86/X86CodeOpt.c",
            "src/Compiler/Backend/X86/X86Codegen.c",
            "src/Compiler/Backend/X86/X86RegAlloc.c",
            "src/Compiler/Pipeline/CompilerPipeline.c",
            "src/Compiler/Build/BuildSystem.c",
            "src/Compiler/Platform/PlatformAbstraction.c",
            "Shared/BytecodeGenerator.c",
            "src/Tools/KroWriter.c",
            "src/Accelerator.c",
            "src/Main.c",
        },
        .flags = c_flags,
    });

    inline for (common_includes) |inc| {
        krtc_module.addIncludePath(inc);
    }

    krtc_module.linkSystemLibrary("m", .{});
    krtc_module.linkSystemLibrary("pthread", .{});

    krtc_module.linkSystemLibrary("arklink", .{});
    krtc_module.addLibraryPath(b.path("../ArkLink/build"));

    const krtc_install = b.addInstallFile(krtc.getEmittedBin(), "../build/KrtC");
    b.getInstallStep().dependOn(&krtc_install.step);

    const run_krtc_cmd = b.addRunArtifact(krtc);
    run_krtc_cmd.step.dependOn(&krtc_install.step);
    const run_krtc_step = b.step("run", "Run the KrtC compiler");
    run_krtc_step.dependOn(&run_krtc_cmd.step);
}
