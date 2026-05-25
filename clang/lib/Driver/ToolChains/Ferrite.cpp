//===--- Ferrite.cpp - Ferrite ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Ferrite.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

void ferrite::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple &Triple = ToolChain.getTriple();

  // Prefer the ld64.lld shipped with this clang, fall back to whatever
  // the standard linker-path machinery returns (honors -fuse-ld).
  std::string LinkerPath = ToolChain.GetProgramPath("ld64.lld");
  if (LinkerPath == "ld64.lld")
    LinkerPath = ToolChain.GetLinkerPath(nullptr);
  const char *Linker = Args.MakeArgString(LinkerPath);

  ArgStringList CmdArgs;

  CmdArgs.push_back("-arch");
  switch (Triple.getArch()) {
  case llvm::Triple::aarch64:
    CmdArgs.push_back("arm64");
    break;
  case llvm::Triple::x86_64:
    CmdArgs.push_back("x86_64");
    break;
  default:
    D.Diag(diag::err_target_unknown_triple) << Triple.str();
    return;
  }

  // Ferrite userspace is statically linked, there is no dyld yet.
  CmdArgs.push_back("-static");

  // ld64.lld requires a -platform_version load command. The "ferrite"
  // platform (PLATFORM_FERRITE=100 in MachO.def) is non-Apple and lives
  // outside Apple's sequential ID range to avoid future collisions.
  CmdArgs.push_back("-platform_version");
  CmdArgs.push_back("ferrite");
  CmdArgs.push_back("1.0");
  CmdArgs.push_back("1.0");

  CmdArgs.push_back("-no_uuid");

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  // No crt files: ferrite-libc.a provides _start via its own crt0.
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (ToolChain.ShouldLinkCXXStdlib(Args))
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
    CmdArgs.push_back("-lferrite_libc");
    AddRunTimeLibs(ToolChain, D, CmdArgs, Args);
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Linker, CmdArgs, Inputs, Output));
}

Ferrite::Ferrite(const Driver &D, const llvm::Triple &Triple,
                 const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);

  auto SysRoot = getDriver().SysRoot;
  if (!SysRoot.empty())
    getFilePaths().push_back(SysRoot + "/lib");
}

void Ferrite::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc))
    addSystemInclude(DriverArgs, CC1Args,
                     getDriver().ResourceDir + "/include");

  if (!DriverArgs.hasArg(options::OPT_nostdlibinc)) {
    auto SysRoot = getDriver().SysRoot;
    if (!SysRoot.empty())
      addSystemInclude(DriverArgs, CC1Args, SysRoot + "/include");
  }
}

void Ferrite::addClangTargetOptions(const ArgList &DriverArgs,
                                    ArgStringList &CC1Args,
                                    Action::OffloadKind) const {
  // Ferrite doesn't exactly support PIE yet.
}

Tool *Ferrite::buildLinker() const {
  return new tools::ferrite::Linker(*this);
}
