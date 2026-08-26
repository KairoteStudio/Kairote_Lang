const std = @import("std");

pub const minimum_zig_version = "0.17.0";

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const common_includes = &.{
        b.path("src"),
        b.path("src/Core"),
        b.path("src/Tools"),
        b.path("src/Bytecode"),
        b.path("Shared"),
        b.path("stub_include"),
        b.path("vm"),
    };

    const c_flags = &.{
        "-Wall",
        "-Wextra",
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-unused-parameter",
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
            "src/compiler/Driver/ArkLinkIntegration.c",
            "src/compiler/Driver/Compiler.c",
            "src/compiler/Driver/ConfigManager.c",
            "src/compiler/Driver/ConsoleUtils.c",
            "src/compiler/Driver/ParallelCompiler.c",
            "src/compiler/Driver/ParallelCompilerLink.c",
            "src/compiler/Driver/Preprocessor.c",
            "src/compiler/Driver/Project.c",
            "src/compiler/Driver/ProjectKrt.c",
            "src/compiler/Driver/TaskManager.c",
            "src/compiler/Frontend/Lexer/Tokenizer.c",
            "src/compiler/Frontend/Parser/Ast.c",
            "src/compiler/Frontend/Parser/Parser.c",
            "src/compiler/Frontend/Parser/ParserAdvanced.c",
            "src/compiler/Frontend/Parser/ParserBase.c",
            "src/compiler/Frontend/Parser/ParserExpression.c",
            "src/compiler/Frontend/Parser/ParserStatement.c",
            "src/compiler/Frontend/CompilerError.c",
            "src/compiler/Frontend/Semantic/Generics.c",
            "src/compiler/Frontend/Semantic/NameMangling.c",
            "src/compiler/Frontend/Semantic/SemanticAnalyzer.c",
            "src/compiler/Frontend/Semantic/SymbolTable.c",
            "src/compiler/Middle/Codegen/IrGen.c",
            "src/compiler/Middle/Ir/Ir.c",
            "src/compiler/Middle/Ir/IrMemory.c",
            "src/compiler/Middle/Ir/IrOptimizer.c",
            "src/compiler/Middle/Ir/IrParamTable.c",
            "src/compiler/Middle/Ir/IrSsa.c",
            "src/compiler/Middle/Ir/IrType.c",
            "src/compiler/Backend/Kro/KroCodegen.c",
            "src/compiler/Backend/Vm/VmCodegen.c",
            "src/compiler/Backend/X86/X86CodeOpt.c",
            "src/compiler/Backend/X86/X86Codegen.c",
            "src/compiler/Backend/X86/X86RegAlloc.c",
            "src/compiler/Pipeline/CompilerPipeline.c",
            "src/compiler/Build/BuildSystem.c",
            "src/compiler/Platform/PlatformAbstraction.c",
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
